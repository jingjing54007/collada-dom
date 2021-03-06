/*
 * Copyright 2006 Sony Computer Entertainment Inc.
 *
 * Licensed under the MIT Open Source License, for details please see license.txt or the website
 * http://www.opensource.org/licenses/mit-license.php
 *
 */
#include <RT.pch.h> //PCH

#include "CrtRender.h"
#include "CrtTexture.h"

COLLADA_(namespace)
{
	namespace RT
	{//-.
//<-----'

void RT::Image::DeleteTexture()
{												
	if(TexId!=0)
	glDeleteTextures(1,&TexId);
	TexId = 0;	
}

//SCHEDULED FOR REMOVAL
//RT::Frame::Load swap-clears this afterward.
COLLADA_(extern) std::vector<char> CrtTexture_buffer(0);

bool RT::Image::Load()
{		
	daeURI absURI; absURI.setURI(DocURI,URL); 

	daeIORequest req(&RT::Main.DOM,nullptr,&absURI,&absURI);	

	return RT::Texture::Load(req);
}
bool RT::Texture::Load(const daeIORequest &req)
{
	daeEH::Verbose<<"Reloading Texture...\n"
	"Resource is "<<req.remoteURI->getURI();

	#ifndef IL_VERSION //LEGACY: TGA-ONLY?
	{
		if(!req.localURI->getURI_extensionIs("tga"))
		return false;
	}
	#endif

	//This is the system for mapping URIs to memory files.	 
	daeIOSecond<daeIOPlugin::Demands::map> I(req);
	daeIOEmpty O;	
	daeRAII::CloseIO RAII(req.scope->getIOController());
	daeIO *IO = RAII = RAII.p.openIO(I,O);
	if(IO==nullptr)
	return false;  
	size_t size = IO->getLock();
	if(size==0)
	return false;
	const void *buf = IO->getReadMap();
	if(buf==nullptr)
	{
		//RT::Main.Load is managing this buffer.
		CrtTexture_buffer.resize(size);
		if(!IO->readIn(&CrtTexture_buffer[0],size))
		return false;
		buf = &CrtTexture_buffer[0];
	}	

	#ifndef IL_VERSION //LEGACY: TGA-ONLY?
	{
		return nullptr!=RT::LoadTargaFromMemory(buf,size,this);
	}
	#else //...
	
	//Must this be 0 terminated?	
	daeRefView ext = req.localURI->getURI_extension();
	char ext0 = '\0';
	std::swap(ext0,(char&)ext.view[ext.extent]);
	ILenum type = ilTypeFromExt(ext.view-1); //Must have a dot.
	std::swap(ext0,(char&)ext.view[ext.extent]);
	if(type==IL_TYPE_UNKNOWN)
	{
		//Maybe ilLoadL(IL_TYPE_UNKNOWN,buf,size); can guess the 
		//type? There doesn't seem to be an API for guessing the
		//type given its data.
		#ifdef NDEBUG
		#error Use IO->readMIME() and <hex format>?
		#endif
		assert(0); return false;
	}

	#ifdef NDEBUG
	#error WHY NOT const void*? TRACE THIS.
	#endif
	if(IL_TRUE!=ilIsValidL(type,(void*)buf,(ILuint)size))
	{
		daeEH::Error<<"DevIL image library believes this resource invalid: \n"<<req.localURI;
		assert(0); return false;
	}
	
	ILuint image;
	ilGenImages(1,&image); ilBindImage(image);

	//This makes images appear in their logical
	//way (right side up) instead of stored way.
	ilEnable(IL_ORIGIN_SET);
	
	#ifdef NDEBUG
	#error What of this?
	#endif
	//These aren't working in any combination.
	//Need to contact maintainer or use another
	//library that will work with unowned memory.
	////ilEnable(IL_FILE_OVERWRITE);
	//ilSetData((void*)buf); 
	//ilSetInteger(IL_IMAGE_SIZE_OF_DATA,size); 
	//ilSetInteger(IL_IMAGE_TYPE,type); 	
	ilLoadL(type,buf,(ILuint)size);
	 	 
	int bpp = 0;	
	switch(ilGetInteger(IL_IMAGE_FORMAT))
	{
	case IL_LUMINANCE:

		bpp = 1; Format = GL_LUMINANCE; break;

	case IL_LUMINANCE_ALPHA: //TODO: Use 2

		bpp = 2; Format = GL_LUMINANCE_ALPHA; break;

	case IL_RGBA: case IL_BGRA: 
	
		bpp = 4; Format = GL_RGBA; break;

	default: bpp = 3; Format = GL_RGB; break;
	}	
	
	Width = ilGetInteger(IL_IMAGE_WIDTH);
	Height = ilGetInteger(IL_IMAGE_HEIGHT);	

	//There was an error in the old code.
	//ILint paddedwidth = (width+3)&(~3);
	int gl_Width = Width*bpp/**(CHAR_BIT/8)*/;
	while(0!=(gl_Width&3)) gl_Width++;

	//Reminder: RT::Image::Refresh() immediately deletes this data.
	#ifdef NDEBUG
	#error Must the data be backed by yet another buffer?
	#endif
	if(Data!=nullptr) COLLADA_RT_array_delete(Data);
	Data = COLLADA_RT_array_new(char,gl_Width*Height);
		
	//NOTE: This works because devIL's enums match OpenGL's.
	ilCopyPixels(0,0,0,Width,Height,1,Format,GL_UNSIGNED_BYTE,(int*)Data);

	ilDeleteImage(image); return true;

	#endif //IL_VERSION

	return false;
}

bool RT::Image::Refresh(bool reload)
{
	//Assuming <hex> or embedded image if so.
	if(URL==nullptr) return true;

	//This is for non OpenGL applications to
	//use. Or just to not show textures.
	if(!RT::Main.LoadImages&&reload==true)
	{
		//Allocate an OpenGL texture ID, even
		//if it's not used for drawing things.
		if(TexId==0) glGenTextures(1,&TexId);
		return true;
	}
				  
	//try to load the file from the path given. If it fails go to the backup		
	if(reload&&!Load())
	{
		daeEH::Warning<<"Failed.";
		return false;
	}	

	//RT::Main.Stack.CreateTexture(this);
	if(Data!=nullptr)
	{	
		if(TexId==0) glGenTextures(1,&TexId);
				
		//Textures are not working. Trying everything????
		int mipmap = 0?GL_LINEAR:GL_LINEAR_MIPMAP_LINEAR;

		//Create Nearest Filtered Texture
		glBindTexture(GL_TEXTURE_2D,TexId);
				
		int iFormat = Format;
		if(sRGB&&glIsEnabled(GL_FRAMEBUFFER_SRGB))
		switch(Format)
		{		
		case GL_RGB: iFormat = GL_SRGB; break;
		case GL_RGBA: iFormat = GL_SRGB_ALPHA; break;
		case GL_LUMINANCE: iFormat = GL_SLUMINANCE; break;
		case GL_LUMINANCE_ALPHA:  iFormat = GL_SLUMINANCE_ALPHA; break;
		default: assert(0);
		}

		//2017: Remove GLU dependency.
		//This API calls glTexImage2D.
		//gluBuild2DMipmaps(GL_TEXTURE_2D,Format,Width,Height,Format,GL_UNSIGNED_BYTE,Data);
		glTexImage2D(GL_TEXTURE_2D,0,iFormat,Width,Height,0,Format,GL_UNSIGNED_BYTE,Data);
		
		//Note: cfxSampler.cpp does this according to the min-filter.
		//(So this--while harmless--may not be right place for this.)
		if(mipmap==GL_LINEAR_MIPMAP_LINEAR)
		{
			#ifdef SN_TARGET_PS3
			glGenerateMipmapOES(GL_TEXTURE_2D);
			#else 		
			glEnable(GL_TEXTURE_2D);
			GL.GenerateMipmap(GL_TEXTURE_2D); //glGenerateMipmap
			#endif
		}
		//Windows Subsystem for Linux is hitting this???
		//assert(!glGetError());

		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mipmap);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,4);

		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

		daeEH::Verbose<<"Done."; //return true;
	}

	//2017: There's no sense keeping a copy since glTexImage2D does.
	//(The legacy code that refers to RT::Texture may use its Data.)
	if(reload) COLLADA_RT_array_delete(Data); return true;	
}

//SCHEDULED FOR REMOVAL?
RT::Texture * /*C4138*//*RT::*/LoadTargaFromURI(const daeURI &URI)
{
	daeIORequest req(RT::Main.DOM,nullptr,&URI,&URI);

	RT::Texture tmp;
	if(!tmp.Load(req)) return nullptr;

	RT::Texture *out = COLLADA_RT_new(RT::Texture);

	*out = tmp; tmp.Data = nullptr; return out;
}

bool RT::Resource::Locate(const xs::anyURI &URI)
{
	Locate(); 
	
	daeIORequest req(URI.getDOM(),nullptr,&URI,&URI);

	//This is the system for mapping URIs to memory files.	
	daeIOSecond<> I(req); daeIOEmpty O; 
	daeRAII::CloseIO RAII(req.scope->getIOController());
	daeIO *IO = RAII = RAII.p.openIO(I,O);
	if(IO==nullptr)
	return false;  
	Size = IO->getLock();
	if(Size==0)
	return false;
	Data = COLLADA_RT_array_new(char,Size+1);
	if(!IO->readIn((void*)Data,Size))
	{
		Locate();
		return false;
	}	
	const_cast<char*&>(Data)[Size-1] = '\0';
	daeEH::Verbose<<"Loaded URI "<<URI.getURI()<<".\n"
	"(Size is "<<Size<<".)";	
	return true;
}
  
//-------.
	}//<-'
}

/*C1071*/
