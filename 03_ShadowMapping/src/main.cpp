#include <ctime>
#include <cstdlib>
#include <cmath>
#include <iostream>

#include <osg/ref_ptr>
#include <osg/Switch>
#include <osg/Group>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/AlphaFunc>
#include <osgGA/StateSetManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgDB/ReadFile>
#include <osg/Light>
#include <osg/LightSource>

osg::ref_ptr<osg::Geometry> createQuads()
{
	// create a quads as geometry
	osg::ref_ptr<osg::Vec3Array>	vertexArray = new osg::Vec3Array;
	vertexArray->push_back(osg::Vec3(-20.0f, -20.0f, -3.0f));
	vertexArray->push_back(osg::Vec3(20.0f, -20.0f, -3.0f));
	vertexArray->push_back(osg::Vec3(-20.0f, 20.0f, -3.0f));
	vertexArray->push_back(osg::Vec3(20.0f, 20.0f, -3.0f));

	osg::ref_ptr<osg::DrawElementsUByte> primitive = new osg::DrawElementsUByte(GL_TRIANGLES);
	primitive->push_back(0); primitive->push_back(1); primitive->push_back(2);
	primitive->push_back(3); primitive->push_back(2); primitive->push_back(1);

	osg::ref_ptr<osg::Vec3Array>        normalArray = new osg::Vec3Array;
	normalArray->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
	normalArray->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
	normalArray->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
	normalArray->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));

	osg::ref_ptr<osg::Vec2Array>		texCoords = new osg::Vec2Array;
	texCoords->push_back(osg::Vec2(0.0f, 0.0f));
	texCoords->push_back(osg::Vec2(1.0f, 0.0f));
	texCoords->push_back(osg::Vec2(0.0f, 1.0f));
	texCoords->push_back(osg::Vec2(1.0f, 1.0f));

	osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
	geometry->setVertexArray(vertexArray);
	geometry->setNormalArray(normalArray);
	geometry->setNormalBinding(osg::Geometry::BIND_PER_VERTEX);
	geometry->setTexCoordArray(0, texCoords);
	geometry->addPrimitiveSet(primitive);

	return geometry;
}

int main(int argc, char** argv)
{
	// Cow.
	osg::ref_ptr<osg::Node> cow = osgDB::readNodeFile("data/cow.osg");
	osg::BoundingSphere bs = cow->getBound();
	// Depth texture.
	osg::ref_ptr<osg::Texture2D> depthTexture = new osg::Texture2D;
	depthTexture->setTextureSize(1024, 1024);
	depthTexture->setInternalFormat(GL_RGBA16F);
	depthTexture->setSourceType(GL_FLOAT);
	depthTexture->setSourceFormat(GL_RGBA);
	// Directional light source.
	osg::Vec3f lightDirection(0, 1, 1);
	lightDirection.normalize();
	// Shadow pass camera.
	osg::ref_ptr<osg::Camera> shadowPassCamera = new osg::Camera;
	shadowPassCamera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // ???
	shadowPassCamera->setClearColor(osg::Vec4f(1000.0f, 0.0f, 0.0f, 1.0f));
	shadowPassCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	shadowPassCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	shadowPassCamera->setRenderOrder(osg::Camera::PRE_RENDER);
	shadowPassCamera->setViewport(0, 0, 1024, 1024);
	osg::Matrixf projectionMatrix = osg::Matrixf::ortho(-bs.radius(),
                                                        bs.radius(),
                                                        -bs.radius(),
                                                        bs.radius(),
                                                        1.0,
                                                        1000.0);
	osg::Vec3f eye = bs.center() + (lightDirection * bs.radius());
	osg::Matrixf viewMatrix = osg::Matrixf::lookAt(eye,
                                                   bs.center(),
                                                   osg::Vec3f(0.0f, 0.0f, 1.0f));
	shadowPassCamera->setProjectionMatrix(projectionMatrix);
	shadowPassCamera->setViewMatrix(viewMatrix);
	shadowPassCamera->attach(osg::Camera::COLOR_BUFFER, depthTexture);
	shadowPassCamera->addChild(cow);
	osg::Matrixf shadowMatrix = viewMatrix *
                                projectionMatrix *
                                osg::Matrixf::translate(1.0, 1.0, 1.0) *
                                osg::Matrixf::scale(0.5, 0.5, 0.5);
	// Shadow pass shader
	osg::ref_ptr<osg::Program> program = new osg::Program;
	program->addShader(osgDB::readShaderFile("shader/shadow_pass.vert"));
	program->addShader(osgDB::readShaderFile("shader/shadow_pass.frag"));
    osg::ref_ptr<osg::StateSet> ss = shadowPassCamera->getOrCreateStateSet();
    ss->setAttributeAndModes(program,
                             osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	osg::ref_ptr<osg::Geode> geode = new osg::Geode;
	geode->addDrawable(createQuads());
	// Main pass shader.
	osg::ref_ptr<osg::Program> program_untextured = new osg::Program;
	program_untextured->addShader(osgDB::readShaderFile("shader/main_pass_untextured.vert"));
	program_untextured->addShader(osgDB::readShaderFile("shader/main_pass_untextured.frag"));
	ss = geode->getOrCreateStateSet();
    ss->setAttributeAndModes(program_untextured,
                             osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setTextureAttributeAndModes(0, depthTexture);
	ss->addUniform(new osg::Uniform("depthTexture", 0));
	ss->addUniform(new osg::Uniform("shadowMatrix", shadowMatrix));
	ss->addUniform(new osg::Uniform("shadowViewMatrix", viewMatrix));
	// Main pass shader.
	osg::ref_ptr<osg::Program> program_main = new osg::Program;
	program_main->addShader(osgDB::readShaderFile("shader/main_pass.vert"));
	program_main->addShader(osgDB::readShaderFile("shader/main_pass.frag"));
	ss = cow->getOrCreateStateSet();
    ss->setAttributeAndModes(program_main, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
	ss->setTextureAttributeAndModes(1, depthTexture);
	ss->addUniform(new osg::Uniform("colorTexture", 0));
	ss->addUniform(new osg::Uniform("depthTexture", 1));
	ss->addUniform(new osg::Uniform("shadowMatrix", shadowMatrix));
	ss->addUniform(new osg::Uniform("shadowViewMatrix", viewMatrix));
	// Scene.
	osg::ref_ptr<osg::Group> scene = new osg::Group;
	scene->addChild(geode);
	scene->addChild(cow);
	scene->addChild(shadowPassCamera);
	scene->getOrCreateStateSet()->addUniform(new osg::Uniform("lightDir", lightDirection));
	osg::ref_ptr<osgViewer::Viewer> viewer = new osgViewer::Viewer;
	viewer->setUpViewInWindow(100, 100, 800, 600);
	viewer->setSceneData(scene);
	return viewer->run();
}

