/*
 * Copyright 2006 Sony Computer Entertainment Inc.
 *
 * Licensed under the MIT Open Source License, for details please see license.txt or the website
 * http://www.opensource.org/licenses/mit-license.php
 *
 */
#include <ColladaDOM.inl> //PCH

//The user can choose whether or not to include libxml support in the DOM. Supporting libxml will
//require linking against it. By default libxml support is included.
#ifdef BUILDING_IN_LIBXML //////////////////////////////////////////
			
 //DID THIS SUCCEED?
//This is a rework of the XML plugin that contains a complete interface to libxml2 "readXML"
//This is intended to be a seperate plugin but I'm starting out by rewriting it in daeLibXMLPlugin
//because I'm not sure if all the plugin handling stuff has been tested.  Once I get a working
//plugin I'll look into renaming it so the old daeLibXMLPlugin can coexist with it.
// - WHO WROTE THIS?
#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>

COLLADA_(namespace)
{//-.
//<-'

int daeLibXMLPlugin::_errorRow()
{
	#if LIBXML_VERSION >= 20620
	return xmlTextReaderGetParserLineNumber(_reader);
	#else
	return -1;
	#endif
}

daeLibXMLPlugin::daeLibXMLPlugin(int legacy):_saveRawFile()
{
	xmlInitParser(); 

	if(legacy!=0)
	{
		if(0!=(legacy&daePlatform::LEGACY_LIBXML_RAW))
		option_to_write_COLLADA_array_values_to_RAW_file_resource();

		if(0!=(legacy&daePlatform::LEGACY_EXTENDED_LATIN1))
		option_to_use_codec_Latin1();
	}

	//If this fails a default _encoder/_decoder is 
	//required and the (xmlChar*) casts have to go.
	daeCTC<sizeof(xmlChar)==sizeof(daeStringCP)>();

	//2/3 is to give US a heads up
	daeCTC<(__combined_size_on_client_stack*2/3>=sizeof(*this))>();
}
daeLibXMLPlugin::~daeLibXMLPlugin(){ xmlCleanupParser(); }
 
//This an old feature request.
//_CD is extended Latin. Put UTF8 into the _X_CD buffer.
//It isn't said why the document isn't just written with a Latin declaration.
void daeLibXMLPlugin::option_to_use_codec_Latin1()
{
	daeCTC<sizeof(daeStringCP)==sizeof(xmlChar)>();
	struct _ //SCOPING. THESE MAY NEED TO BE GLOBALS IN ORDER TO  DEBUG THEM.
	{
	static daeHashString UTF8ToLatin1(const daeHashString &UTF, daeArray<daeStringCP> &Latin)
	{
		int inLen = (int)UTF.extent;
		int outLen = (inLen+1)*2; Latin.grow(outLen);
		int numBytes = UTF8Toisolat1((xmlChar*)Latin.data(),&outLen,(xmlChar*)UTF.string,&inLen);
		if(numBytes<0)
		{
			numBytes = 0; //Failed. Return an empty string instead.
		}
		Latin.data()[numBytes] = '\0';
		Latin.getAU()->setInternalCounter(numBytes); return Latin;
	}
	static daeHashString Latin1ToUTF8(daeArray<daeStringCP> &Latin, daeArray<daeStringCP> &UTF)
	{
		int inLen = (int)Latin.size();
		int outLen = (inLen+1)*2; UTF.grow(outLen);
		//isolat1ToUTF8 is include/libxml/encoding.h.
		int numBytes = isolat1ToUTF8((xmlChar*)UTF.data(),&outLen,(xmlChar*)Latin.data(),&inLen);
		if(numBytes<0)
		{
			numBytes = 0; //Failed. Return an empty string instead.			
		}
		UTF.data()[numBytes] = '\0';
		UTF.getAU()->setInternalCounter(numBytes); return UTF;
	}};
	_encoder = _::UTF8ToLatin1; _decoder = _::Latin1ToUTF8;
}

static void daeLibXMLPlugin_libxmlErrorHandler
(void *arg, const char *msg, xmlParserSeverities severity, xmlTextReaderLocatorPtr locator)
{
	if(severity==XML_PARSER_SEVERITY_VALIDITY_WARNING||severity==XML_PARSER_SEVERITY_WARNING)
	{
		daeEH::Warning<<msg<<" at line "<<xmlTextReaderLocatorLineNumber(locator);
	}
	else daeEH::Error<<msg<<" at line "<<xmlTextReaderLocatorLineNumber(locator);
}

//Previously ::xmlTextReaderHelper.
//A simple structure to help alloc/free xmlTextReader objects
struct daeLibXMLPlugin::Reader
{ 
	//Who owns this macro???
	#if LIBXML_VERSION < 20700
	#ifndef XML_PARSE_HUGE
	#define XML_PARSE_HUGE 0
	#endif
	#endif
	//Open question: What do XML_READER_TYPE_ENTITY_REFERENCE and XML_READER_TYPE_ENTITY refer to?
	//xmlIsBlankNode: Checks whether this node is an empty or whitespace only (and possibly ignorable) text-node
	enum{ ops=XML_PARSE_HUGE|XML_PARSE_NOCDATA|XML_PARSE_NOENT/*ITIES*/|XML_PARSE_COMPACT|XML_PARSE_NOBLANKS };
	
	//Documentation is poor.
	//https://github.com/mapnik/mapnik/issues/440 says:
	//The ``base_url`` keyword argument allows to set the original base URL of
	//the document to support relative Paths when looking up external entities
	//(DTD, XInclude, ...).
	#ifdef NDEBUG
	#error Is the URL really required? If not, remove _fixUriForLibxml.
	#endif
	
	typedef std::pair<daeIO*,int> _xmlInputReadCallback_pair;
	static int _xmlInputReadCallback(void *IO_read, daeStringCP *buffer, int len)
	{
		_xmlInputReadCallback_pair &p = *(_xmlInputReadCallback_pair*)IO_read;
		len = std::min(len,p.second); p.second-=len;
		return DAE_OK==p.first->readIn(buffer,len)?len:-1;
	}
	static int _xmlInputCloseCallback(void *IO_read)
	{
		return ((_xmlInputReadCallback_pair*)IO_read)->second==0?0:-1; 
	}
	xmlTextReader *reader; _xmlInputReadCallback_pair read;
	Reader():reader(),read(){}
	~Reader(){ if(reader!=nullptr) xmlFreeTextReader(reader); }		
	void _set(xmlTextReader *r)
	{
		assert(reader==nullptr);
		if((reader=r)!=nullptr)
		xmlTextReaderSetErrorHandler(reader,daeLibXMLPlugin_libxmlErrorHandler,nullptr); 
	}
	void set_up_xmlReaderForFd(int fd)
	{
		_set(xmlReaderForFd(fd,nullptr,nullptr,ops));
	}
	void set_up_xmlReaderForIO(daeIO &IO)
	{
		read.first = &IO; read.second = (int)IO.getLock();
		_set(xmlReaderForIO(_xmlInputReadCallback,_xmlInputCloseCallback,&read,nullptr,nullptr,ops));
	}		
	void set_up_xmlReaderForMemory(const daeHashString &string)
	{
		_set(xmlReaderForMemory(string,(int)string.extent,nullptr,nullptr,ops)); 
	}
};

bool daeLibXMLPlugin::_read(daeIO &IO, daeContents &content)
{
	const daeIORequest &req = getRequest();

	//This used to be two APIs.
	Reader RAII;	
	if(nullptr!=req.remoteURI)
	{
		//Reminder: Windows file descriptors are bound to the CRT.
		char nonnull;
		//Reminder: libXML prints confusing errors if length is 0.
		if(0!=IO.getLock())
		if(IO.readIn(&nonnull,0)!=DAE_ERR_NOT_IMPLEMENTED)
		RAII.set_up_xmlReaderForIO(IO);
		/*Want to remove the FILE based APIs from daeIO to simplify.
		else
		RAII.set_up_xmlReaderForFd(getCRT()->FILE.stat(IO.getReadFILE()).fd);
		*/
		if(RAII.reader==nullptr)
		{
			daeEH::Error<<"In daeLibXMLPlugin::readFromFile...";
			return false;
		}
	}
	else if(nullptr!=req.string)
	{
		//Reminder: libXML prints confusing errors if length is 0.
		if(!req.string.empty())
		RAII.set_up_xmlReaderForMemory(req.string);
		if(RAII.reader==nullptr)
		{
			daeEH::Error<<"In daeLibXMLPlugin::readFromMemory...";
			return false;
		}
	}
	else //This is unexpected.
	{
		daeEH::Error<<"daeLibXMLPlugin I/O request is neither file-descriptor, nor memory-string...";
		return false;
	}

	//_reader is for _errorRow().
	_reader = RAII.reader; 
		  
	//This is the only way to get this.
	xmlTextReaderRead(_reader);	
	daeIOPluginCommon::_push_back_xml_decl(content,
	(daeString)xmlTextReaderConstXmlVersion(_reader),
	(daeString)xmlTextReaderConstEncoding(_reader),
	1==xmlTextReaderStandalone(_reader));		

	int readRetVal = _readContent2(content.getElement());
	if(-1==readRetVal) return false;
	//1 or 0?
	//THE RETURN CODES ARE COMPLETELY UNDOCUMENTED???
	assert(0<=readRetVal); return true;
}

int daeLibXMLPlugin::_readElement(daePseudoElement &parent)
{
	//This was an argument. It should fetch better as a stack object.
	xmlTextReader *reader = _reader; 

	assert(xmlTextReaderNodeType(reader)==XML_READER_TYPE_ELEMENT);
	daeString elementName = (daeString)xmlTextReaderConstName(reader);
	
	//Is this needed? It seems to need to be called before attributes.
	int empty = xmlTextReaderIsEmptyElement(reader);

	while(xmlTextReaderMoveToNextAttribute(reader)==1)
	{
		daeString xmlName = (daeString)xmlTextReaderConstName(reader);
		daeString xmlValue = (daeString)xmlTextReaderConstValue(reader);
		daeIOPluginCommon::_attribs.push_back(_attrPair(xmlName,xmlValue));
	}
	daeElement &element = 
	daeIOPluginCommon::_beginReadElement(parent,elementName);		
	/*Post-2.5 all elements are accepted in order to prevent loss. This is not a validator.
	if(element==nullptr)
	{
		//We couldn't create the element. beginReadElement already printed an error message. Just make sure
		//to skip ahead past the bad element.
		xmlTextReaderNext(reader);
		return nullptr;
	}*/

	//Is this required? This assertion fails.
	//assert(empty==xmlTextReaderIsEmptyElement(reader));
	int readRetVal = xmlTextReaderRead(reader); 
	if(1==empty||readRetVal==-1) 
	return readRetVal;
	return _readContent2(element);
}
int daeLibXMLPlugin::_readContent2(daePseudoElement &parent)
{
	//This was an argument. It should fetch better as a stack object.
	xmlTextReader *reader = _reader; 
					  
	int nodeType = xmlTextReaderNodeType(reader);
	int readRetVal = 1;
	while(readRetVal==1&&nodeType!=XML_READER_TYPE_END_ELEMENT)
	{
		if(nodeType!=XML_READER_TYPE_ELEMENT) 
		{
			switch(nodeType)
			{		
			case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:

				daeCTC<Reader::ops&XML_PARSE_NOBLANKS>();
				//assert(0);
				#ifdef NDEBUG
				#error What can be done about this?
				#endif
				break;

			case XML_READER_TYPE_CDATA:

				daeCTC<Reader::ops&XML_PARSE_NOCDATA>();
				#ifdef NDEBUG
				#error Add CDATA flag to daeText::_.reserved?
				#endif
				//break;

			case XML_READER_TYPE_TEXT: 
			
				_readElementText(parent,(daeString)xmlTextReaderConstValue(reader));			
				break;
			
			case XML_READER_TYPE_COMMENT:
			
				parent.getContents().push_back<'!'>(_encode(xmlTextReaderConstValue(reader),_CD));
				break;

			case XML_READER_TYPE_XML_DECLARATION:

				//This never appears??? The first node is XML_READER_TYPE_NONE. Perhaps
				//because xmlTextReaderRead has not been called, but then the next node
				//is XML_READER_TYPE_ELEMENT (or whatever is first.)
				assert(0);
				break;

			case XML_READER_TYPE_PROCESSING_INSTRUCTION:
			{
				daeHashString target = _encode(xmlTextReaderConstName(reader),_CD);
				if(xmlTextReaderHasValue(reader))
				{
					//HACK: repurposing _X_CD as a secondary buffer.
					_X_CD.assign(target).push_back(' ');
					_X_CD.append(_encode(xmlTextReaderConstValue(reader),_CD));
					parent.getContents().push_back<'?'>(_X_CD);
				}
				else parent.getContents().push_back<'?'>(target);
				break;
			}}
			readRetVal = xmlTextReaderRead(reader);
		}
		else readRetVal = _readElement(parent); 
		
		nodeType = xmlTextReaderNodeType(reader);
	}

	//1 or 0?
	//THE RETURN CODES ARE COMPLETELY UNDOCUMENTED???
	//if(1==readRetVal)
	if(nodeType==XML_READER_TYPE_END_ELEMENT)
	{
		//1 or 0?
		//THE RETURN CODES ARE COMPLETELY UNDOCUMENTED???
		assert(1==readRetVal);

		readRetVal = xmlTextReaderRead(reader);
	}

	//IS -1 THE ONLY BAD CODE??
	//Something went wrong (bad xml probably)
	assert(readRetVal!=-1);
	return readRetVal;
}
 
daeOK daeLibXMLPlugin::writeContent(daeIO &IO, const daeContents &content)
{	
	int err;
	const daeIORequest &req = getRequest();

	daeURI rawURI; //Open secondary I/O channel?
	daeIORequest rawReq(req.scope,nullptr,&rawURI);
	daeIOEmpty rawI; 
	daeIOSecond<> rawO(rawReq); 
	if(_saveRawFile)
	{
		const daeURI *l = req.remoteURI;
		l->getURI_baseless(_raw);
		_raw.insert(daeURI_parser(_raw).getURI_uptoCP<'?'>(),".raw");
		rawURI.setURI(_raw);
		rawURI.setParentObject(content.getElement());
		_rawIO = rawReq.scope->getDOM()->getPlatform().openIO(rawI,rawO);
		if(_rawIO==nullptr)
		{
			daeEH::Error<<"RAW: Couldn't open secondary I/O channel for daeLibXMLPlugin::option_to_write_COLLADA_array_values_to_RAW_file_resource.";
			if(_rawIO!=nullptr) goto RawFILE_is_0;
			return DAE_ERR_BACKEND_IO;		
		}
		_rawByteCount = 0;
	}

	struct _ //TODO? LIBXMLWriter
	{
		static int _xmlOutputWriteCallback(void *IO, daeString buffer, int len)
		{
			return DAE_OK==((daeIO*)IO)->writeOut(buffer,len)?len:-1;
		}
		static int _xmlOutputCloseCallback(void*){ return 0; }
	};
	_writer = xmlNewTextWriter(xmlOutputBufferCreateIO(_::_xmlOutputWriteCallback,_::_xmlOutputCloseCallback,&IO,nullptr));
	
	if(_writer==nullptr)
	{
		daeEH::Error<<
		"daeLibXMLPlugin::write("<<req.remoteURI->getURI()<<")\n"
		"Error creating the LIBXML writer.";		
		return DAE_ERR_BACKEND_IO;
	}
	//Don't change "\t" to " ". --UNKNOWN AUTHOR (Why?)
	err = xmlTextWriterSetIndentString(_writer,(const xmlChar*)"\t");
	assert(err>=0);
	err = xmlTextWriterSetIndent(_writer,1); //Turns indentation on.
	assert(err>=0);	
	const char *version,*encoding,*standalone;
	daeIOPluginCommon::_xml_decl(content,version,encoding,standalone);	
	err = xmlTextWriterStartDocument(_writer,version,encoding,standalone);
	assert(err>=0);

	IO.getLock();
	_OK = IO.getError();
	if(_OK) _writeContent2(content);

	xmlTextWriterEndDocument(_writer);
	xmlTextWriterFlush(_writer);
	xmlFreeTextWriter(_writer);
		
	if(_saveRawFile)
	{
		if(DAE_OK!=_rawIO->getError()) RawFILE_is_0: 
		{	
			_OK = DAE_ERR_BACKEND_IO; daeEH::Error<<"Raw FILE error: "<<_raw;			
		}
		rawReq.scope->getDOM()->getPlatform().closeIO(_rawIO);		
	}
	
	return _OK;
}

void daeLibXMLPlugin::_writeElement(const daeElement &element)
{
	daeMeta *meta = element->getMeta();
	const daeContents &content = meta->getContentsWRT(&element);

	//RAW: Intercept <source> elements for special handling.
	if(_saveRawFile&&"source"==element->getNCName())
	{
		const daeElement *child;
		const daeElement *array = nullptr, *technique_common = nullptr;
		for(size_t i=0;i<content.size();i++) if(content[i].hasChild())
		{
			child = content[i];
			const daePseudonym &name = content[i].getChild()->getNCName();
			if("float_array"==name||"int_array"==name)
			array = child;
			else if("technique_common"==name)
			technique_common = child;				
		}
		if(array!=nullptr&&technique_common!=nullptr)
		{
			_writeRawSource(element,*array,*technique_common);
			return;
		}
	}

	xmlTextWriterStartElement(_writer,(xmlChar*)element.getNCName().string);
	{
		const daeArray<daeAttribute> &attrs = meta->getAttributes();		
		for(size_t i=0,iN = attrs.size();i<iN;i++) 
		_writeAttribute(attrs[i],element);
		//Previously this came after _writeValue(element),
		//-and they were not treated as mutually exclusive.
		_writeContent2(element.getContents());
		_writeValue(element);	
	}
	xmlTextWriterEndElement(_writer);	
}

void daeLibXMLPlugin::_writeContent2(const daeContents &content)
{
	for(size_t i=0,iN=content.size();i<iN;) if(content[i].hasText())
	{
		daeText &text = content[i]; 
		if(text.getText_increment(i,_CD).empty())
		continue;
		const xmlChar *CDecoded = _decode(_CD,_X_CD);
		switch(text.kind())
		{			
		case daeKindOfText::COMMENT:

			//xmlTextWriterStartComment(_writer);
			xmlTextWriterWriteComment(_writer,CDecoded);
			//xmlTextWriterEndComment(_writer);
			break;

		case daeKindOfText::PI_LIKE:
		
			if("xml "==daeName(_CD.data(),4))
			break;
			//Note, the parameter is called "target" but
			//it seems to accept target & value together.
			xmlTextWriterStartPI(_writer,CDecoded);
			xmlTextWriterEndPI(_writer);
			break;

		case daeKindOfText::MIXED:

			xmlTextWriterWriteString(_writer,CDecoded);
			break;

		default: assert(0); 
		}	
	}
	else _writeElement(content[i++]);
}

void daeLibXMLPlugin::_writeAttribute(daeAttribute &attr, const daeElement &element)
{
	attr->memoryToStringWRT(&element,_CD);

	//REMINDER: TO SUPPORT LEGACY BEHAVIOR, <COLLADA> HAS
	//BOTH-REQUIRED-AND-DEFAULT ON ITS version ATTRIBUTES.
	//Don't write the attribute if
	//  - The attribute isn't required AND
	//     - The attribute has no default value and the current value is ""
	//     - The attribute has a default value and the current value matches the default
	if(!attr->getIsRequired())	
	if(attr->getDefaultString()==nullptr)
	{
		if(_CD.empty())	return;
	}
	else if(0==attr->compareToDefaultWRT(&element))
	return;

	xmlTextWriterStartAttribute(_writer,(xmlChar*)attr.getName().string);
	
	if(_maybeExtendedASCII(attr))
	{
		//This an old feature request.
		//_CD is extended Latin. Put UTF8 into the _X_CD buffer.
		//It isn't said why the document isn't just written with a Latin declaration.
		xmlTextWriterWriteString(_writer,_decode(_CD,_X_CD));
	}
	else xmlTextWriterWriteString(_writer,(xmlChar*)_CD.data());	

	xmlTextWriterEndAttribute(_writer);
}

void daeLibXMLPlugin::_writeValue(const daeElement &element)
{
	if(!element.getCharData(_CD).empty()) 
	if(_maybeExtendedASCII(*element.getCharDataObject()))
	{
		//This an old feature request.
		//_CD is extended Latin. Put UTF8 into the _X_CD buffer.
		//It isn't said why the document isn't just written with a Latin declaration.
		xmlTextWriterWriteString(_writer,_decode(_CD,_X_CD));
	}
	else xmlTextWriterWriteString(_writer,(xmlChar*)_CD.data());	
}

void daeLibXMLPlugin::_writeRawSource(const daeElement &src, 
#ifdef NDEBUG //GCC doesn't like apostrophes.
#error "clone()???? clone()???? clone()???? clone()???? Don't use clone()."
#endif									  
const daeElement &unused_array, const daeElement &unused_technique_common)
{
	daeElementRef array, newSrc = src.clone(); //???	
	daeHashString name;
	daeElement *accessor = nullptr;
	daeContents &content = newSrc->getContents();	
	for(size_t i=0;i<content.size();i++) if(content[i].hasChild())
	{
		daeElement *child = content[i];
		name = child->getNCName();
		if("int_array"==name||"float_array"==name)
		{
			assert(array==nullptr); array = child; 
		}
		else if("technique_common"==name)
		{
			daeContents &content = child->getContents();	
			for(size_t i=0;i<content.size();i++) if(content[i].hasChild())
			{
				child = content[i];
				if("accessor"==child->getNCName())
				{
					accessor = child; break;
				}
			}//NEW: Can never be too careful.
			if(accessor==nullptr) 
			accessor = newSrc->add("accessor"); break;
		}
	}//Caller guarantees array.
	//Clone could fail (Cloning shouldn't even be necessaary.)
	assert(array!=nullptr); 
	newSrc->removeChildElement(array);
			 
	#ifdef NDEBUG
	#error THIS SEEMS HIGHLY DUBIOUS???
	#endif
	size_t stride_floor = accessor->getChildrenCount();		
	#ifdef NDEBUG
	#error The types are not guaranteed to be daeULong. (Or even to exist.)
	#endif
	//There probably should be better APIs for this sort of thing.	
	daeULong &stride = accessor->getAttributeObject("stride")->getWRT(accessor);	
	if(stride_floor>stride) stride = stride_floor;
	daeAttribute *count = array->getAttributeObject("count");
	assert(count->getSize()==sizeof(daeULong)
	&&(const daeULong&)count->getWRT(array)<COLLADA_UPTR_MAX);
	size_t i,iN = (const size_t&)count->getWRT(array);
	
	daeStringCP a[33] = "#";
	sprintf(a+1,"%d",(int)_rawByteCount); 
	size_t snip = _raw.size(); _raw.append(a);
	accessor->setAttribute("source",_raw);
	_raw.erase(snip);	
			 
	//TODO: pay attention to precision for the array.
	i = 0;
	#define _(x,y,z) if(atomic_type==daeAtomicType::z)\
	{\
		x tmp; _rawByteCount+=sizeof(x)*iN;\
		for(const y&i0=valArray->getRaw();i<iN;i++)\
		_rawIO->writeOut(&(tmp=(&i0)[i]),sizeof(x));\
	}else
	daeCharData *arrayCD = array->getCharDataObject();
	int atomic_type = arrayCD->getType()->where<daeAtom>().getAtomicType();	
	const daeAlloc<> *valArray = (daeAlloc<>*const&)arrayCD->getWRT(array);
	COLLADA_SUPPRESS_C(4244) //possible loss of data
	_(int,daeInt,INT)_(int,daeLong,LONG)_(float,daeFloat,FLOAT)_(float,daeDouble,DOUBLE)
	{
		_OK = DAE_ERR_BACKEND_IO;
		daeEH::Error<<_raw<<"\n"
		"Could not write RAW file data.\n" 
		"daeAtomicType is not INT nor LONG nor FLOAT nor DOUBLE.";
	}
	#undef _

	_writeElement(*newSrc);
}

//---.
}//<-'

#endif //BUILDING_IN_LIBXML/////////////////////////////////////////////////

/*C1071*/
