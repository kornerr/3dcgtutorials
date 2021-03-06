/*
	The MIT License (MIT)

	Copyright (c) 2013 Marcel Pursche

	Permission is hereby granted, free of charge, to any person obtaining a copy of
	this software and associated documentation files (the "Software"), to deal in
	the Software without restriction, including without limitation the rights to
	use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
	the Software, and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
	FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
	COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
	IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
	CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "InstancedGeometryBuilder.h"
#include "InstancedDrawable.h"

// std
#include <cstring>

// osg
#include <osg/Uniform>
#include <osg/Group>
#include <osg/Geode>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osg/Image>
#include <osg/TextureRectangle>
#include <osg/BufferObject>
#include <osg/BufferIndexBinding>

// osgExample
#include "ComputeInstanceBoundingBoxCallback.h"
#include "ComputeTextureBoundingBoxCallback.h"
#include "MatrixUniformUpdateCallback.h"

namespace osgExample
{

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::getSoftwareInstancedNode() const
{
	// create Group to contain all instances
	osg::ref_ptr<osg::Group>	group = new osg::Group;

	// create Geode to wrap Geometry
	osg::ref_ptr<osg::Geode>	geode = new osg::Geode;
	geode->addDrawable(m_geometry);

	// now create a MatrixTransform for each matrix in the list
	for (auto it = m_matrices.begin(); it != m_matrices.end(); ++it)
	{
		osg::ref_ptr<osg::MatrixTransform> matrixTransform = new osg::MatrixTransform(*it);

		matrixTransform->addChild(geode);
		group->addChild(matrixTransform);
	}

	osg::ref_ptr<osg::Program> program = new osg::Program;
	osg::ref_ptr<osg::Shader> vsShader = osgDB::readShaderFile("../shader/no_instancing.vert");
	osg::ref_ptr<osg::Shader> fsShader = osgDB::readShaderFile("../shader/no_instancing.frag");
	program->addShader(vsShader);
	program->addShader(fsShader);

	group->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);
	
	return group;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::getHardwareInstancedNode() const
{
	osg::ref_ptr<osg::Node> instancedNode;

	// first check if we need to split up the geometry in groups
	if (m_matrices.size() <= m_maxMatrixUniforms)
	{
		// we don't have more matrices than uniform space so we only need one geode
		instancedNode = createHardwareInstancedGeode(0, m_matrices.size());
	} else {
		// we need to split up instances into several geodes
		osg::ref_ptr<osg::Group> group = new osg::Group;

		unsigned int numGeodes = m_matrices.size() / m_maxMatrixUniforms;
		for (unsigned int i = 0; i < numGeodes; ++i)
		{
			unsigned int start = i*m_maxMatrixUniforms;
			unsigned int end    = std::min((unsigned int)m_matrices.size(), (start + m_maxMatrixUniforms));
			group->addChild(createHardwareInstancedGeode(start, end));
		}
		instancedNode = group;
	}

	osg::ref_ptr<osg::Program> program = new osg::Program;
		
	std::stringstream preprocessorDefinition;
	preprocessorDefinition << "#define MAX_INSTANCES " << m_maxMatrixUniforms;
	osg::ref_ptr<osg::Shader> vsShader = readShaderFile("../shader/instancing.vert", preprocessorDefinition.str());
	osg::ref_ptr<osg::Shader> fsShader = osgDB::readShaderFile("../shader/instancing.frag");
	program->addShader(vsShader);
	program->addShader(fsShader);

	instancedNode->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

	return instancedNode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::getTextureHardwareInstancedNode() const
{
	osg::ref_ptr<osg::Node> instancedNode;

	// first check if we need to split up the geometry in groups
	if (m_matrices.size() <= m_maxTextureResolution)
	{
		// we don't have more matrices than uniform space so we only need one geode
		instancedNode = createTextureHardwareInstancedGeode(0, m_matrices.size());
	} else {
		// we need to split up instances into several geodes
		osg::ref_ptr<osg::Group> group = new osg::Group;

		unsigned int numGeodes = m_matrices.size() / m_maxTextureResolution;
		for (unsigned int i = 0; i < numGeodes; ++i)
		{
			unsigned int start = i*m_maxTextureResolution;
			unsigned int end    = std::min((unsigned int)m_matrices.size(), (start + m_maxTextureResolution));
			group->addChild(createTextureHardwareInstancedGeode(start, end));
		}
		instancedNode = group;
	}

	
	// add shaders
	osg::ref_ptr<osg::Program> program = new osg::Program;
	osg::ref_ptr<osg::Shader> vsShader = osgDB::readShaderFile("../shader/texture_instancing.vert");
	osg::ref_ptr<osg::Shader> fsShader = osgDB::readShaderFile("../shader/texture_instancing.frag");
	program->addShader(vsShader);
	program->addShader(fsShader);

	instancedNode->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

	return instancedNode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::getUBOHardwareInstancedNode() const
{
	osg::ref_ptr<osg::Node> instancedNode;

	m_floatArrays.clear();

	unsigned int maxUBOMatrices = (m_maxUniformBlockSize / 64);

	// first check if we need to split up the geometry in groups
	if (m_matrices.size() <= maxUBOMatrices)
	{
		// we don't have more matrices than uniform space so we only need one geode
		instancedNode = createUBOHardwareInstancedGeode(0, m_matrices.size(), maxUBOMatrices);
	} else {
		// we need to split up instances into several geodes
		osg::ref_ptr<osg::Group> group = new osg::Group;

		unsigned int numGeodes = m_matrices.size() / maxUBOMatrices;
		for (unsigned int i = 0; i < numGeodes; ++i)
		{
			unsigned int start = i*maxUBOMatrices;
			unsigned int end    = std::min((unsigned int)m_matrices.size(), (start + maxUBOMatrices));
			group->addChild(createUBOHardwareInstancedGeode(start, end, maxUBOMatrices));
		}
		instancedNode = group;
	}

	
	// add shaders
	osg::ref_ptr<osg::Program> program = new osg::Program;
	std::stringstream preprocessorDefinition;
	preprocessorDefinition << "#define MAX_INSTANCES " << maxUBOMatrices;
	osg::ref_ptr<osg::Shader> vsShader = readShaderFile("../shader/ubo_instancing.vert", preprocessorDefinition.str());
	osg::ref_ptr<osg::Shader> fsShader = osgDB::readShaderFile("../shader/ubo_instancing.frag");
	program->addShader(vsShader);
	program->addShader(fsShader);
	program->addBindUniformBlock("instanceData", 0);

	instancedNode->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

	return instancedNode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::getVertexAttribHardwareInstancedNode() const
{
	// create custom instanced drawable
	osg::ref_ptr<InstancedDrawable> drawable = new InstancedDrawable;
	drawable->setVertexArray(dynamic_cast<osg::Vec3Array*>(m_geometry->getVertexArray()));
	drawable->setNormalArray(dynamic_cast<osg::Vec3Array*>(m_geometry->getNormalArray()));
	drawable->setTexCoordArray(dynamic_cast<osg::Vec2Array*>(m_geometry->getTexCoordArray(0)));
	
	osg::ref_ptr<osg::DrawElementsUByte> instancedPrimitive = dynamic_cast<osg::DrawElementsUByte*>(m_geometry->getPrimitiveSet(0)->clone(osg::CopyOp::DEEP_COPY_ALL));
	instancedPrimitive->setNumInstances(m_matrices.size());
	drawable->setDrawElements(instancedPrimitive);
	drawable->setMatrixArray(m_matrices);

	// create geode and program to wrap the drawable
	osg::ref_ptr<osg::Geode> geode = new osg::Geode;
	geode->addDrawable(drawable);

	osg::ref_ptr<osg::Program> program = new osg::Program;
	osg::ref_ptr<osg::Shader> vsShader = osgDB::readShaderFile("../shader/attribute_instancing.vert");
	osg::ref_ptr<osg::Shader> fsShader = osgDB::readShaderFile("../shader/attribute_instancing.frag");
	program->addShader(vsShader);
	program->addShader(fsShader);
	program->addBindAttribLocation("vPosition", 0);
	program->addBindAttribLocation("vNormal", 1);
	program->addBindAttribLocation("vTexCoord", 2);
	program->addBindAttribLocation("vInstanceModelMatrix", 3);
	geode->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

	// add matrix uniforms and update callback
	osg::ref_ptr<osgExample::MatrixUniformUpdateCallback> updateCallback = new osgExample::MatrixUniformUpdateCallback;
	geode->getOrCreateStateSet()->addUniform(updateCallback->getModelViewProjectionMatrixUniform());
	geode->getOrCreateStateSet()->addUniform(updateCallback->getNormalMatrixUniform());
	geode->setCullCallback(updateCallback);

	return geode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::createHardwareInstancedGeode(unsigned int start, unsigned int end) const
{
		// we don't have more matrices than uniform space so we only need one geode
		osg::ref_ptr<osg::Geode>	geode = new osg::Geode;
		osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry(*m_geometry, osg::CopyOp::DEEP_COPY_ALL);
		geode->addDrawable(geometry);

		// first turn on hardware instancing for every primitive set
		for (unsigned int i = 0; i < geometry->getNumPrimitiveSets(); ++i)
		{
			geometry->getPrimitiveSet(i)->setNumInstances(end-start);
		}

		// we need to turn off display lists for instancing to work
		geometry->setUseDisplayList(false);
		geometry->setUseVertexBufferObjects(true);

		// create uniform array for matrices
		osg::ref_ptr<osg::Uniform> instanceMatrixUniform = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "instanceModelMatrix", end-start);

		for (unsigned int i = start, j = 0; i < end; ++i, ++j)
		{
			instanceMatrixUniform->setElement(j, m_matrices[i]);
		}
		geode->getOrCreateStateSet()->addUniform(instanceMatrixUniform);
			
		// add bounding box callback so osg computes the right bounding box for our geode
		geometry->setComputeBoundingBoxCallback(new ComputeInstancedBoundingBoxCallback(instanceMatrixUniform));

		// add matrix uniforms and update callback
		osg::ref_ptr<osgExample::MatrixUniformUpdateCallback> updateCallback = new osgExample::MatrixUniformUpdateCallback;
		geode->getOrCreateStateSet()->addUniform(updateCallback->getModelViewProjectionMatrixUniform());
		geode->getOrCreateStateSet()->addUniform(updateCallback->getNormalMatrixUniform());
		geode->setCullCallback(updateCallback);

		return geode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::createTextureHardwareInstancedGeode(unsigned int start, unsigned int end) const
{
	osg::ref_ptr<osg::Geode>	geode = new osg::Geode;
	osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry(*m_geometry, osg::CopyOp::DEEP_COPY_ALL);
	geode->addDrawable(geometry);

	// first turn on hardware instancing for every primitive set
	for (unsigned int i = 0; i < geometry->getNumPrimitiveSets(); ++i)
	{
		geometry->getPrimitiveSet(i)->setNumInstances(m_matrices.size());
	}

	// we need to turn off display lists for instancing to work
	geometry->setUseDisplayList(false);
	geometry->setUseVertexBufferObjects(true);
	
	// create texture to encode all matrices
	unsigned int height = ((end-start) / 4096u) + 1u;
	osg::ref_ptr<osg::Image> image = new osg::Image;
	image->allocateImage(16384, height, 1, GL_RGBA, GL_FLOAT);
	image->setInternalTextureFormat(GL_RGBA32F_ARB);

	for (unsigned int i = start, j = 0; i < end; ++i, ++j)
	{
		osg::Matrixf matrix = m_matrices[i];
		float * data = (float*)image->data((j % 4096u) *4u, j / 4096u);
		memcpy(data, matrix.ptr(), 16 * sizeof(float));
	}

	osg::ref_ptr<osg::TextureRectangle> texture = new osg::TextureRectangle(image);
	texture->setInternalFormat(GL_RGBA32F_ARB);
	texture->setSourceFormat(GL_RGBA);
	texture->setSourceType(GL_FLOAT);
	texture->setTextureSize(4, end-start);
	texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_BORDER);
	texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_BORDER);

	geode->getOrCreateStateSet()->setTextureAttributeAndModes(1, texture, osg::StateAttribute::ON);
	geode->getOrCreateStateSet()->addUniform(new osg::Uniform("instanceMatrixTexture", 1));

	// copy part of matrix list and create bounding box callback
	std::vector<osg::Matrixd> matrices;
	matrices.insert(matrices.begin(), m_matrices.begin()+start, m_matrices.begin()+end);
	geometry->setComputeBoundingBoxCallback(new ComputeTextureBoundingBoxCallback(matrices));
	
	// add matrix uniforms and update callback
	osg::ref_ptr<osgExample::MatrixUniformUpdateCallback> updateCallback = new osgExample::MatrixUniformUpdateCallback;
	geode->getOrCreateStateSet()->addUniform(updateCallback->getModelViewProjectionMatrixUniform());
	geode->getOrCreateStateSet()->addUniform(updateCallback->getNormalMatrixUniform());
	geode->setCullCallback(updateCallback);

	return geode;
}

osg::ref_ptr<osg::Node> InstancedGeometryBuilder::createUBOHardwareInstancedGeode(unsigned int start, unsigned int end, unsigned int maxUBOMatrices) const
{
	osg::ref_ptr<osg::Geode>	geode = new osg::Geode;
	osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry(*m_geometry, osg::CopyOp::DEEP_COPY_ALL);
	geode->addDrawable(geometry);

	// first turn on hardware instancing for every primitive set
	for (unsigned int i = 0; i < geometry->getNumPrimitiveSets(); ++i)
	{
		geometry->getPrimitiveSet(i)->setNumInstances(m_matrices.size());
	}

	// we need to turn off display lists for instancing to work
	geometry->setUseDisplayList(false);
	geometry->setUseVertexBufferObjects(true);
	
	// create uniform buffer object for all matrices
	osg::FloatArray* matrixArray = new osg::FloatArray(maxUBOMatrices*16);
	m_floatArrays.push_back(matrixArray);
	for (unsigned int i = start, j = 0; i < end; ++i, ++j)
	{
		for (unsigned int k = 0; k < 16; ++k)
		{
			(*matrixArray)[j*16+k] =  m_matrices[i].ptr()[k];
		}
	}
	osg::ref_ptr<osg::UniformBufferObject> ubo = new osg::UniformBufferObject;
	ubo->setUsage(GL_STATIC_DRAW_ARB);
	ubo->setDataVariance(osg::Object::STATIC);
	matrixArray->setBufferObject(ubo);

	// create uniform buffer binding and add it to the stateset
	osg::ref_ptr<osg::UniformBufferBinding> ubb = new osg::UniformBufferBinding(0, ubo, 0, maxUBOMatrices*16*sizeof(GLfloat));
	geode->getOrCreateStateSet()->setAttributeAndModes(ubb, osg::StateAttribute::ON);

	// copy part of matrix list and create bounding box callback
	std::vector<osg::Matrixd> matrices;
	matrices.insert(matrices.begin(), m_matrices.begin()+start, m_matrices.begin()+end);
	geometry->setComputeBoundingBoxCallback(new ComputeTextureBoundingBoxCallback(matrices));

	// add matrix uniforms and update callback
	osg::ref_ptr<osgExample::MatrixUniformUpdateCallback> updateCallback = new osgExample::MatrixUniformUpdateCallback;
	geode->getOrCreateStateSet()->addUniform(updateCallback->getModelViewProjectionMatrixUniform());
	geode->getOrCreateStateSet()->addUniform(updateCallback->getNormalMatrixUniform());
	geode->setCullCallback(updateCallback);

	return geode;
}

osg::ref_ptr<osg::Shader> InstancedGeometryBuilder::readShaderFile(const std::string& fileName, const std::string& preprocessorDefinitions) const
{
	// open vertex shader file
	std::ifstream vsShaderFile;
    vsShaderFile.open(fileName.c_str(), std::ios_base::in);

	if(!vsShaderFile.is_open()) {
		std::cout << "Error: Could not open shader file " << fileName << std::endl;
		return NULL;
	}

	std::stringstream vsShaderStr;
	std::string line;
	std::getline(vsShaderFile, line);
	vsShaderStr << line << std::endl;
	vsShaderStr << preprocessorDefinitions << std::endl;
	while (!vsShaderFile.eof()) {
		std::getline(vsShaderFile, line);
		vsShaderStr << line << std::endl;
	}
	vsShaderFile.close();

	return new osg::Shader(osg::Shader::VERTEX, vsShaderStr.str());
}

}
