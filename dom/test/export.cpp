#include <dae.h>
#include <dae/daeUtils.h>
#include <dom/domCOLLADA.h>
#include "domTest.h"

using namespace std;
using namespace cdom;

// Demonstrates how to use the DOM to create a simple, textured Collada model
// and save it to disk.

testResult addGeometry(daeElement* root);

DefineTest(export) {
	DAE dae;
	string file = getTmpFile("export.dae");
	domCOLLADA* root = dae.addFile(file.c_str());
	CheckResult(root);

	// An <asset> is required.
	daeElement* asset = root->add("asset");
	asset->add("created")->setCharData("2008-02-23T13:30:00Z");
	asset->add("modified")->setCharData("2008-02-23T13:30:00Z");

	// Create the various <library_whatever> elements we'll need.
	daeElement *geomLib = root->add("library_geometries"),
	           *imageLib = root->add("library_images"),
	           *effectLib = root->add("library_effects"),
	           *materialLib = root->add("library_materials"),
	           *visualSceneLib = root->add("library_visual_scenes");
	CheckResult(geomLib && imageLib && effectLib && materialLib && visualSceneLib);

	CheckTestResult(addGeometry(geomLib));

	dae.save();
	
	return testResult(true);
}

template<typename T>
daeTArray<T> rawArrayToDaeArray(T rawArray[], size_t count) {
	daeTArray<T> result;
	for (size_t i = 0; i < count; i++)
		result.append(rawArray[i]);
	return result;
}

// "myGeom" --> "#myGeom"
string makeUriRef(const string& id) {
	return string("#") + id;
}

testResult addSource(daeElement* mesh,
                     const string& srcID,
                     const string& paramNames,
                     domFloat values[],
                     int valueCount) {
	SafeAdd(mesh, "source", src);
	src->setAttribute("id", srcID.c_str());

	domFloat_array* fa = daeSafeCast<domFloat_array>(src->add("float_array"));
	CheckResult(fa);
	fa->setId((src->getAttribute("id") + "-array").c_str());
	fa->setCount(valueCount);
	fa->getValue() = rawArrayToDaeArray(values, valueCount);

	domAccessor* acc = daeSafeCast<domAccessor>(src->add("technique_common accessor"));
	CheckResult(acc);
	acc->setSource(makeUriRef(fa->getId()).c_str());

	list<string> params = tokenize(paramNames, " ");
	acc->setStride(params.size());
	acc->setCount(valueCount);
	for (tokenIter iter = params.begin(); iter != params.end(); iter++) {
		SafeAdd(acc, "param", p);
		p->setAttribute("name", iter->c_str());
		p->setAttribute("type", "float");
	}
	
	return testResult(true);
}

testResult addInput(daeElement* triangles,
                    const string& semantic,
                    const string& srcID,
                    int offset) {
	domInputLocalOffset* input = daeSafeCast<domInputLocalOffset>(triangles->add("input"));
	CheckResult(input);
	input->setSemantic(semantic.c_str());
	input->setOffset(offset);
	input->setSource(makeUriRef(srcID).c_str());
	if (semantic == "TEXCOORD")
		input->setSet(0);
	return testResult(true);
}

testResult addGeometry(daeElement* geomLib) {
	string geomID = "cubeGeom";
	SafeAdd(geomLib, "geometry", geom);
	geom->setAttribute("id", geomID.c_str());
	SafeAdd(geom, "mesh", mesh);

	// Add the position data
	domFloat posArray[] = { -10, -10, -10,
	                        -10, -10,  10,
	                        -10,  10, -10,
	                        -10,  10,  10,
	                         10, -10, -10,
	                         10, -10,  10,
	                         10,  10, -10,
	                         10,  10,  10 };
	int count = sizeof(posArray)/sizeof(posArray[0]);
	CheckTestResult(addSource(mesh, geomID + "-positions", "X Y Z", posArray, count));

	// Add the normal data
	domFloat normalArray[] = {  1,  0,  0,
	                           -1,  0,  0,
	                            0,  1,  0,
	                            0, -1,  0,
	                            0,  0,  1,
	                            0,  0, -1 };
	count = sizeof(normalArray)/sizeof(normalArray[0]);
	CheckTestResult(addSource(mesh, geomID + "-normals", "X Y Z", normalArray, count));

	// Add the tex coord data
	domFloat uvArray[] = { 0, 0,
	                       0, 1,
	                       1, 0,
	                       1, 1 };
	count = sizeof(uvArray)/sizeof(uvArray[0]);
	CheckTestResult(addSource(mesh, geomID + "-uv", "S T", uvArray, count));

	// Add the <vertices> element
	SafeAdd(mesh, "vertices", vertices);
	vertices->setAttribute("id", (geomID + "-vertices").c_str());
	SafeAdd(vertices, "input", verticesInput);
	verticesInput->setAttribute("semantic", "POSITION");
	verticesInput->setAttribute("source", makeUriRef(geomID + "-positions").c_str());

	// Add the <triangles> element.
	// Each line is one triangle.
	domUint	indices[] = {	0, 1, 0,   1, 1, 1,   2, 1, 2,
	                      1, 1, 1,   3, 1, 3,   2, 1, 2,
	                      0, 2, 0,   4, 2, 1,   1, 2, 2,
	                      4, 2, 1,   5, 2, 3,   1, 2, 2,
	                      1, 4, 0,   5, 4, 1,   3, 4, 2,
	                      5, 4, 1,   7, 4, 3,   3, 4, 2,
	                      5, 0, 0,   4, 0, 1,   7, 0, 2,
	                      4, 0, 1,   6, 0, 3,   7, 0, 2,
	                      4, 5, 0,   0, 5, 1,   6, 5, 2,
	                      0, 5, 1,   2, 5, 3,   6, 5, 2,
	                      3, 3, 0,   7, 3, 1,   2, 3, 2,
	                      7, 3, 1,   6, 3, 3,   2, 3, 2 };
	count = sizeof(indices)/sizeof(indices[0]);

	domTriangles* triangles = daeSafeCast<domTriangles>(mesh->add("triangles"));
	CheckResult(triangles);
	triangles->setCount(count/3);
	triangles->setMaterial("mtl");

	CheckTestResult(addInput(triangles, "VERTEX",   geomID + "-vertices", 0));
	CheckTestResult(addInput(triangles, "NORMAL",   geomID + "-normals",  1));
	CheckTestResult(addInput(triangles, "TEXCOORD", geomID + "-uv",       2));

	domP* p = daeSafeCast<domP>(triangles->add("p"));
	CheckResult(p);
	p->getValue() = rawArrayToDaeArray(indices, count);

	return testResult(true);
}