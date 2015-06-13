/* xoreos - A reimplementation of BioWare's Aurora engine
 *
 * xoreos is the legal property of its developers, whose names
 * can be found in the AUTHORS file distributed with this source
 * distribution.
 *
 * xoreos is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * xoreos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with xoreos. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  Loading MMH+MSH files found in Dragon Age: Origins and Dragon Age 2.
 */

/* Based on tazpn's DAO tools (<http://social.bioware.com/project/218/>)
 * and DA2 tools (<http://social.bioware.com/project/4253/>), both
 * released under the terms of the 3-clause BSD license, as well
 * as the documentation on the Dragon Age Toolset wiki
 * (<http://social.bioware.com/wiki/datoolset/index.php/Model>).
 */

#include "src/common/util.h"
#include "src/common/strutil.h"
#include "src/common/error.h"
#include "src/common/maths.h"
#include "src/common/stream.h"
#include "src/common/encoding.h"
#include "src/common/filepath.h"
#include "src/common/xml.h"

#include "src/aurora/resman.h"
#include "src/aurora/aurorafile.h"
#include "src/aurora/gff4file.h"

#include "src/graphics/aurora/model_dragonage.h"

// Disable the "unused variable" warnings while most stuff is still stubbed
IGNORE_UNUSED_VARIABLES

namespace Graphics {

namespace Aurora {

// .--- GFF4 helpers
static const uint32 kMMHID     = MKTAG('M', 'M', 'H', ' ');
static const uint32 kMSHID     = MKTAG('M', 'E', 'S', 'H');
static const uint32 kVersion01 = MKTAG('V', '0', '.', '1');
static const uint32 kVersion10 = MKTAG('V', '1', '.', '0');

static const uint32 kNODEID    = MKTAG('n', 'o', 'd', 'e');
static const uint32 kMSHHID    = MKTAG('m', 's', 'h', 'h');
static const uint32 kCRSTID    = MKTAG('c', 'r', 's', 't');

static const uint32 kTRSLID    = MKTAG('t', 'r', 's', 'l');
static const uint32 kROTAID    = MKTAG('r', 'o', 't', 'a');

static const uint32 kCHNKID    = MKTAG('c', 'h', 'n', 'k');
static const uint32 kDECLID    = MKTAG('d', 'e', 'c', 'l');

static const uint32 kGFFID     = MKTAG('G', 'F', 'F', ' ');
static const uint32 kXMLID     = MKTAG('<', '?', 'x', 'm');

static const uint32 kMAOID     = MKTAG('M', 'A', 'O', ' ');

static const uint32 kFLOTID    = MKTAG('f', 'l', 'o', 't');
static const uint32 kFLT4ID    = MKTAG('f', 'l', 't', '4');
static const uint32 kTEXID     = MKTAG('t', 'e', 'x', ' ');

using ::Aurora::GFF4File;
using ::Aurora::GFF4Struct;
using ::Aurora::GFF4List;

using ::Aurora::kFileTypeMMH;
using ::Aurora::kFileTypeMSH;
using ::Aurora::kFileTypeMAO;

using namespace ::Aurora::GFF4FieldNamesEnum;

static const GFF4Struct *getChild(const GFF4Struct &children, uint i) {
	bool isList;
	GFF4Struct::FieldType type = children.getFieldType(i, isList);
	if ((type != GFF4Struct::kFieldTypeStruct) || isList)
		return 0;

	return children.getStruct(i);
}

static bool isType(const GFF4Struct *strct, uint32 type) {
	return strct && (strct->getLabel() == type);
}

static bool isType(const GFF4Struct &strct, uint32 type) {
	return isType(&strct, type);
}

static const GFF4Struct *findMeshChunk(const GFF4Struct &mshTop, const Common::UString &name) {
	const GFF4List &chunks = mshTop.getList(kGFF4MeshChunks);
	for (GFF4List::const_iterator c = chunks.begin(); c != chunks.end(); ++c) {
		if (!isType(*c, kCHNKID))
			continue;

		if ((*c)->getString(kGFF4Name).equalsIgnoreCase(name))
			return *c;
	}

	return 0;
}
// '--- GFF4 helpers


Model_DragonAge::ParserContext::ParserContext(const Common::UString &name) :
	mmh(0), msh(0), mmhTop(0), mshTop(0), state(0) {

	try {

		open(name);

	} catch (Common::Exception &e) {
		delete msh;
		delete mmh;

		e.add("Failed to load model \"%s\"", name.c_str());
		throw e;
	}
}

void Model_DragonAge::ParserContext::open(const Common::UString &name) {
	mmhFile = name.toLower();

	// Open the MMH

	mmh = new GFF4File(mmhFile, kFileTypeMMH, kMMHID);
	if (mmh->getTypeVersion() != kVersion01)
		throw Common::Exception("Unsupported MMH version %s", Common::debugTag(mmh->getTypeVersion()).c_str());

	mmhTop = &mmh->getTopLevel();

	// Read the MSH file name

	mshFile = mmhTop->getString(kGFF4MMHModelHierarchyModelDataName).toLower();
	mshFile = Common::FilePath::changeExtension(mshFile, "");
	if (mshFile.empty())
		throw Common::Exception("MMH has no associated MSH");

	// Open the MSH

	msh = new GFF4File(mshFile, kFileTypeMSH, kMSHID);
	if ((msh->getTypeVersion() != kVersion01) && (msh->getTypeVersion() != kVersion10))
		throw Common::Exception("Unsupported MSH version %s", Common::debugTag(msh->getTypeVersion()).c_str());

	mshTop = &msh->getTopLevel();

	// Read the internal MMH and MSH names

	mmhName = Common::FilePath::changeExtension(mmhTop->getString(kGFF4MMHName), "").toLower();
	mshName = Common::FilePath::changeExtension(mshTop->getString(kGFF4Name)   , "").toLower();
}

Model_DragonAge::ParserContext::~ParserContext() {
	delete msh;
	delete mmh;

	clear();
}

void Model_DragonAge::ParserContext::clear() {
	for (std::list<ModelNode_DragonAge *>::iterator n = nodes.begin(); n != nodes.end(); ++n)
		delete *n;
	nodes.clear();

	delete state;
	state = 0;
}


Model_DragonAge::Model_DragonAge(const Common::UString &name, ModelType type) : Model(type) {
	_fileName = name;

	ParserContext ctx(name);

	load(ctx);

	finalize();
}

Model_DragonAge::~Model_DragonAge() {
}

void Model_DragonAge::load(ParserContext &ctx) {
	_name = ctx.mmhName;

	// Lax sanity checks
	if (ctx.mmhFile != ctx.mmhName)
		warning("MMH names don't match (\"%s\" vs. \"%s\")", ctx.mmhFile.c_str(), ctx.mmhName.c_str());
	if (ctx.mshFile != ctx.mshName)
		warning("MSH names don't match (\"%s\" vs. \"%s\")", ctx.mshFile.c_str(), ctx.mshName.c_str());

	const GFF4Struct *rootNodes = ctx.mmhTop->getGeneric(kGFF4MMHChildren);
	if (!rootNodes)
		return;

	newState(ctx);

	// Create root nodes
	for (uint i = 0; i < rootNodes->getFieldCount(); i++) {
		const GFF4Struct *nodeGFF = getChild(*rootNodes, i);
		if (!isType(nodeGFF, kNODEID))
			continue;

		ModelNode_DragonAge *rootNode = new ModelNode_DragonAge(*this);
		ctx.nodes.push_back(rootNode);

		rootNode->load(ctx, *nodeGFF);
	}

	addState(ctx);
}

void Model_DragonAge::newState(ParserContext &ctx) {
	ctx.clear();

	ctx.state = new State;
}

void Model_DragonAge::addState(ParserContext &ctx) {
	if (!ctx.state || ctx.nodes.empty()) {
		ctx.clear();
		return;
	}

	for (std::list<ModelNode_DragonAge *>::iterator n = ctx.nodes.begin(); n != ctx.nodes.end(); ++n) {
		ctx.state->nodeList.push_back(*n);
		ctx.state->nodeMap.insert(std::make_pair((*n)->getName(), *n));

		if (!(*n)->getParent())
			ctx.state->rootNodes.push_back(*n);
	}

	_stateList.push_back(ctx.state);
	_stateMap.insert(std::make_pair(ctx.state->name, ctx.state));

	if (!_currentState)
		_currentState = ctx.state;

	ctx.state = 0;

	ctx.nodes.clear();
}


ModelNode_DragonAge::ModelNode_DragonAge(Model &model) : ModelNode(model) {
}

ModelNode_DragonAge::~ModelNode_DragonAge() {
}

// .--- Vertex value reading helpers
void ModelNode_DragonAge::read2Float32(Common::ReadStream &stream, MeshDeclType type, float *&f) {
	switch (type) {
		case kMeshDeclTypeFloat32_2:
		case kMeshDeclTypeFloat32_3:
		case kMeshDeclTypeFloat32_4:
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			break;

		case kMeshDeclTypeSint16_2:
		case kMeshDeclTypeSint16_4:
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			break;

		case kMeshDeclTypeSint16_2n:
		case kMeshDeclTypeSint16_4n:
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			break;

		case kMeshDeclTypeUint16_2n:
		case kMeshDeclTypeUint16_4n:
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			break;

		case kMeshDeclTypeFloat16_2:
		case kMeshDeclTypeFloat16_4:
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			break;

		default:
			throw Common::Exception("Invalid data type for 2 floats: %u", (uint) type);
	}
}

void ModelNode_DragonAge::read3Float32(Common::ReadStream &stream, MeshDeclType type, float *&f) {
	switch (type) {
		case kMeshDeclTypeFloat32_3:
		case kMeshDeclTypeFloat32_4:
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			break;

		case kMeshDeclTypeColor:
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			break;

		case kMeshDeclTypeUint8_4:
			*f++ = stream.readByte();
			*f++ = stream.readByte();
			*f++ = stream.readByte();
			break;

		case kMeshDeclTypeSint16_4:
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			break;

		case kMeshDeclTypeUint8_4n:
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			break;

		case kMeshDeclTypeSint16_4n:
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			break;

		case kMeshDeclTypeUint16_4n:
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			break;

		case kMeshDeclType1010102:
			{
				uint32 data = stream.readUint32LE();

				*f++ = (uint16) ((data >> 22) & 0x3FFF);
				*f++ = (uint16) ((data >> 12) & 0x3FFF);
				*f++ = (uint16) ((data >>  2) & 0x3FFF);
			}
			break;

		case kMeshDeclType1010102n:
			{
				uint32 data = stream.readUint32LE();

				*f++ = (uint16) ((data >> 22) & 0x3FFF) / 511.0;
				*f++ = (uint16) ((data >> 12) & 0x3FFF) / 511.0;
				*f++ = (uint16) ((data >>  2) & 0x3FFF) / 511.0;
			}
			break;

		case kMeshDeclTypeFloat16_4:
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			break;

		default:
			throw Common::Exception("Invalid data type for 3 floats: %u", (uint) type);
	}
}

void ModelNode_DragonAge::read4Float32(Common::ReadStream &stream, MeshDeclType type, float *&f) {
	switch (type) {
		case kMeshDeclTypeFloat32_3:
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			*f++ = 1.0;
			break;

		case kMeshDeclTypeFloat32_4:
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			*f++ = stream.readIEEEFloatLE();
			break;

		case kMeshDeclTypeColor:
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			break;

		case kMeshDeclTypeUint8_4:
			*f++ = stream.readByte();
			*f++ = stream.readByte();
			*f++ = stream.readByte();
			*f++ = stream.readByte();
			break;

		case kMeshDeclTypeSint16_4:
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			*f++ = (int16) stream.readUint16LE();
			break;

		case kMeshDeclTypeUint8_4n:
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			*f++ = stream.readByte() / 255.0;
			break;

		case kMeshDeclTypeSint16_4n:
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			*f++ = ((int16) stream.readUint16LE()) / 32767.0;
			break;

		case kMeshDeclTypeUint16_4n:
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			*f++ = stream.readUint16LE() / 65535.0;
			break;

		case kMeshDeclType1010102:
			{
				uint32 data = stream.readUint32LE();

				*f++ = (uint16) ((data >> 22) & 0x3FFF);
				*f++ = (uint16) ((data >> 12) & 0x3FFF);
				*f++ = (uint16) ((data >>  2) & 0x3FFF);
				*f++ = (uint16) ( data        & 0x0002);
			}
			break;

		case kMeshDeclType1010102n:
			{
				uint32 data = stream.readUint32LE();

				*f++ = (uint16) ((data >> 22) & 0x3FFF) / 511.0;
				*f++ = (uint16) ((data >> 12) & 0x3FFF) / 511.0;
				*f++ = (uint16) ((data >>  2) & 0x3FFF) / 511.0;
				*f++ = (uint16) ( data        & 0x0002) /   4.0;
			}
			break;

		case kMeshDeclTypeFloat16_4:
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			*f++ = readIEEEFloat16(stream.readUint16LE());
			break;

		default:
			throw Common::Exception("Invalid data type for 4 floats: %u", (uint) type);
	}
}
// '--- Vertex value reading helpers

void ModelNode_DragonAge::load(Model_DragonAge::ParserContext &ctx, const GFF4Struct &nodeGFF) {
	_name = nodeGFF.getString(kGFF4MMHName);

	// Read local translation + rotation of this node
	readTransformation(nodeGFF);

	// If this is a mesh node, read the mesh
	if (isType(nodeGFF, kMSHHID))
		readMesh(ctx, nodeGFF);

	// Read the children
	readChildren(ctx, nodeGFF);

	// Create the bounding box
	createBound();
}

void ModelNode_DragonAge::readTransformation(const GFF4Struct &nodeGFF) {
	const GFF4Struct *children = nodeGFF.getGeneric(kGFF4MMHChildren);
	if (!children)
		return;

	for (uint i = 0; i < children->getFieldCount(); i++) {
		const GFF4Struct *childGFF = getChild(*children, i);

		if        (isType(childGFF, kTRSLID)) {
			// Translation

			double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
			childGFF->getVector4(kGFF4MMHTranslation, x, y, z, w);

			_position[0] = x;
			_position[1] = y;
			_position[2] = z;

		} else if (isType(childGFF, kROTAID)) {
			// Rotation

			double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
			childGFF->getVector4(kGFF4MMHRotation, x, y, z, w);

			_orientation[0] = x;
			_orientation[1] = y;
			_orientation[2] = z;
			_orientation[3] = Common::rad2deg(acos(w) * 2.0);
		}
	}
}

void ModelNode_DragonAge::readChildren(Model_DragonAge::ParserContext &ctx, const GFF4Struct &nodeGFF) {
	const GFF4Struct *children = nodeGFF.getGeneric(kGFF4MMHChildren);
	if (!children)
		return;

	for (uint i = 0; i < children->getFieldCount(); i++) {
		const GFF4Struct *childGFF = getChild(*children, i);
		if (!isType(childGFF, kNODEID) && !isType(childGFF, kMSHHID) && !isType(childGFF, kCRSTID))
			continue;

		ModelNode_DragonAge *childNode = new ModelNode_DragonAge(*_model);
		ctx.nodes.push_back(childNode);

		childNode->setParent(this);

		childNode->load(ctx, *childGFF);
	}
}

void ModelNode_DragonAge::sanityCheckMeshChunk(const GFF4Struct &meshChunk) {
	const uint32 primitiveType = meshChunk.getUint(kGFF4MeshChunkPrimitiveType);
	if (primitiveType != 0)
		throw Common::Exception("Unsupported primitive type %u", primitiveType);

	const uint32 indexFormat = meshChunk.getUint(kGFF4MeshChunkIndexFormat);
	if (indexFormat != 0)
		throw Common::Exception("Invalid index format %u", indexFormat);

	const uint32 baseVertexIndex = meshChunk.getUint(kGFF4MeshChunkBaseVertexIndex);
	if (baseVertexIndex != 0)
		throw Common::Exception("Unsupported base vertex index %u", baseVertexIndex);

	const uint32 minIndex = meshChunk.getUint(kGFF4MeshChunkMinIndex);
	if (minIndex != 0)
		throw Common::Exception("Unsupported min index %u", minIndex);
}

void ModelNode_DragonAge::readMeshDecl(const GFF4Struct &meshChunk, MeshDeclarations &meshDecl) {
	const GFF4List &chunkDecl = meshChunk.getList(kGFF4MeshChunkVertexDeclarator);

	for (GFF4List::const_iterator d = chunkDecl.begin(); d != chunkDecl.end(); ++d) {
		// Go through all structs in the declarator list, look for non-empty declaration

		if (!isType(*d, kDECLID))
			continue;

		const int32 streamIndex = (*d)->getSint(kGFF4MeshVertexDeclaratorStream);
		if (streamIndex == -1)
			continue;

		// Sanity checks

		if (streamIndex != 0)
			throw Common::Exception("Unsupported chunk stream index %d", streamIndex);

		const uint32 usageIndex = (*d)->getUint(kGFF4MeshVertexDeclaratorUsageIndex);
		if (streamIndex != 0)
			throw Common::Exception("Unsupported chunk usage index %u", usageIndex);

		const uint32 method = (*d)->getUint(kGFF4MeshVertexDeclaratorMethod);
		if (method != 0)
			throw Common::Exception("Unsupported chunk method %u", method);

		// Is this declaration even used?
		const MeshDeclUse use = (MeshDeclUse) (*d)->getUint(kGFF4MeshVertexDeclaratorUsage, kMeshDeclUseUnused);
		if (use == kMeshDeclUseUnused)
			continue;

		// Read and add the declaration

		const int32 offset = (*d)->getSint(kGFF4MeshVertexDeclaratorOffset);
		const MeshDeclType type = (MeshDeclType) (*d)->getUint(kGFF4MeshVertexDeclaratorDatatype);

		meshDecl.push_back(MeshDeclaration(use, *d, offset, type));
	}
}

void ModelNode_DragonAge::createIndexBuffer(const GFF4Struct &meshChunk,
		Common::SeekableReadStream &indexData) {

	uint32 indexCount = meshChunk.getUint(kGFF4MeshChunkIndexCount);
	_indexBuffer.setSize(indexCount, sizeof(uint16), GL_UNSIGNED_SHORT);

	const uint32 startIndex = meshChunk.getUint(kGFF4MeshChunkStartIndex);
	indexData.skip(startIndex * 2);

	uint16 *indices = (uint16 *) _indexBuffer.getData();
	while (indexCount-- > 0)
		*indices++ = indexData.readUint16LE();
}

void ModelNode_DragonAge::createVertexBuffer(const GFF4Struct &meshChunk,
		Common::SeekableReadStream &vertexData, const MeshDeclarations &meshDecl) {

	const uint32 vertexPos    = vertexData.pos();
	const uint32 vertexSize   = meshChunk.getUint(kGFF4MeshChunkVertexSize);
	const uint32 vertexCount  = meshChunk.getUint(kGFF4MeshChunkVertexCount);
	const uint32 vertexOffset = meshChunk.getUint(kGFF4MeshChunkVertexOffset);

	VertexDecl vertexDecl;

	uint textureCount = 0;
	for (MeshDeclarations::const_iterator d = meshDecl.begin(); d != meshDecl.end(); ++d) {
		switch (d->use) {
			case kMeshDeclUsePosition:
				vertexDecl.push_back(VertexAttrib(VPOSITION, 3, GL_FLOAT));
				break;

			case kMeshDeclUseNormal:
				vertexDecl.push_back(VertexAttrib(VNORMAL, 3, GL_FLOAT));
				break;

			case kMeshDeclUseTexCoord:
				vertexDecl.push_back(VertexAttrib(VTCOORD + textureCount++, 2, GL_FLOAT));
				break;

			case kMeshDeclUseColor:
				vertexDecl.push_back(VertexAttrib(VCOLOR, 4, GL_FLOAT));
				break;

			default:
				break;
		}
	}

	_vertexBuffer.setVertexDeclInterleave(vertexCount, vertexDecl);

	float *vData = (float *) _vertexBuffer.getData();
	for (uint32 v = 0; v < vertexCount; v++) {

		for (MeshDeclarations::const_iterator d = meshDecl.begin(); d != meshDecl.end(); ++d) {
			vertexData.seek(vertexPos + vertexOffset + v * vertexSize + d->offset);

			try {
				switch (d->use) {
					case kMeshDeclUsePosition:
						read3Float32(vertexData, d->type, vData);
						break;

					case kMeshDeclUseNormal:
						read3Float32(vertexData, d->type, vData);
						break;

					case kMeshDeclUseTexCoord:
						read2Float32(vertexData, d->type, vData);
						break;

					case kMeshDeclUseColor:
						read4Float32(vertexData, d->type, vData);
						break;

					default:
						break;
				}
			} catch (Common::Exception &e) {
				e.add("While reading mesh declaration with usage %u", d->use);
				throw e;
			}
		}
	}

}

/** Read a MAO encoded in a GFF file. */
void ModelNode_DragonAge::readMAOGFF(Common::SeekableReadStream *maoStream, MaterialObject &material) {
	GFF4File mao(maoStream, kMAOID);

	const GFF4Struct &maoTop = mao.getTopLevel();

	// General properties
	material.material        = maoTop.getString(kGFF4MAOMaterial);
	material.defaultSemantic = maoTop.getString(kGFF4MAODefaultSemantic);

	// Floats
	const GFF4List &floats = maoTop.getList(kGFF4MAOFloats);
	for (GFF4List::const_iterator f = floats.begin(); f != floats.end(); ++f) {
		if (!isType(*f, kFLOTID))
			continue;

		material.floats[(*f)->getString(kGFF4MAOFloatName)] = (float) (*f)->getDouble(kGFF4MAOFloatValue);
	}

	// Vectors
	const GFF4List &vectors = maoTop.getList(kGFF4MAOVectors);
	for (GFF4List::const_iterator v = floats.begin(); v != floats.end(); ++v) {
		if (!isType(*v, kFLT4ID))
			continue;

		double v1 = 0.0, v2 = 0.0, v3 = 0.0, v4 = 1.0;
		(*v)->getVector4(kGFF4MAOVectorValue, v1, v2, v3, v4);

		material.vectors[(*v)->getString(kGFF4MAOVectorName)] = Common::Vector3(v1, v2, v3, v4);
	}

	// Textures
	const GFF4List &textures = maoTop.getList(kGFF4MAOTextures);
	for (GFF4List::const_iterator t = textures.begin(); t != textures.end(); ++t) {
		if (!isType(*t, kTEXID))
			continue;

		material.textures[(*t)->getString(kGFF4MAOTextureName)] = (*t)->getString(kGFF4MAOTextureResource);
	}
}

/** Read a MAO encoded in an XML file. */
void ModelNode_DragonAge::readMAOXML(Common::SeekableReadStream *maoStream, MaterialObject &material) {
	try {

		Common::XMLParser mao(*maoStream);
		const xmlNode &maoRoot = mao.getRoot();

		if (!Common::UString((const char *) maoRoot.name).equalsIgnoreCase("MaterialObject"))
			throw Common::Exception("Invalid XML MAO root element (\"%s\")", maoRoot.name);

		for (xmlNode *child = maoRoot.children; child; child = child->next) {
			const Common::UString name = (const char *) child->name;

			if (name.equalsIgnoreCase("Material")) {

				for (xmlAttr *attrib = child->properties; attrib; attrib = attrib->next)
					if (Common::UString((const char *) attrib->name).equalsIgnoreCase("Name") && attrib->children)
						material.material = (const char *) attrib->children->content;

			} else if (name.equalsIgnoreCase("DefaultSemantic")) {

				for (xmlAttr *attrib = child->properties; attrib; attrib = attrib->next)
					if (Common::UString((const char *) attrib->name).equalsIgnoreCase("Name") && attrib->children)
						material.defaultSemantic = (const char *) attrib->children->content;

			} else if (name.equalsIgnoreCase("Float")) {

				Common::UString fName;
				float value = 0.0f;

				for (xmlAttr *attrib = child->properties; attrib; attrib = attrib->next) {
					if (!attrib->children)
						continue;

					if      (Common::UString((const char *) attrib->name).equalsIgnoreCase("Name"))
						fName = (const char *) attrib->children->content;
					else if (Common::UString((const char *) attrib->name).equalsIgnoreCase("Value"))
						sscanf((const char *) attrib->children->content, "%f", &value);
				}

				material.floats[fName] = value;

			} else if (name.equalsIgnoreCase("Vector4f")) {

				Common::UString vName;
				float v1 = 0.0f, v2 = 0.0f, v3 = 0.0f, v4 = 0.0f;

				for (xmlAttr *attrib = child->properties; attrib; attrib = attrib->next) {
					if (!attrib->children)
						continue;

					if      (Common::UString((const char *) attrib->name).equalsIgnoreCase("Name"))
						vName = (const char *) attrib->children->content;
					else if (Common::UString((const char *) attrib->name).equalsIgnoreCase("Value"))
						sscanf((const char *) attrib->children->content, "%f %f %f %f", &v1, &v2, &v3, &v4);
				}

				material.vectors[vName] = Common::Vector3(v1, v2, v3, v4);

			} else if (name.equalsIgnoreCase("Texture")) {

				Common::UString tName, value;

				for (xmlAttr *attrib = child->properties; attrib; attrib = attrib->next) {
					if (!attrib->children)
						continue;

					if      (Common::UString((const char *) attrib->name).equalsIgnoreCase("Name"))
						tName = (const char *) attrib->children->content;
					else if (Common::UString((const char *) attrib->name).equalsIgnoreCase("ResName"))
						value = (const char *) attrib->children->content;
				}

				material.textures[tName] = value;
			}
		}

	} catch (...) {
		delete maoStream;
		throw;
	}

	delete maoStream;
}

/** Read a material object MAO, which can be encoded in either XML or GFF. */
void ModelNode_DragonAge::readMAO(const Common::UString &materialName, MaterialObject &material) {
	try {

		Common::SeekableReadStream *maoStream = ResMan.getResource(materialName, kFileTypeMAO);
		if (!maoStream)
			throw Common::Exception("No such MAO");

		const uint32 tag = ::Aurora::AuroraBase::readHeaderID(*maoStream);
		maoStream->seek(0);

		if      (tag == kGFFID)
			readMAOGFF(maoStream, material);
		else if (tag == kXMLID)
			readMAOXML(maoStream, material);
		else {
			delete maoStream;
			throw Common::Exception("Invalid MAO type %s", Common::debugTag(tag).c_str());
		}

	} catch (Common::Exception &e) {
		e.add("Failed to load MAO \"%s\"", materialName.c_str());
		throw e;
	}
}

void ModelNode_DragonAge::readMesh(Model_DragonAge::ParserContext &ctx, const GFF4Struct &meshGFF) {
	Common::UString meshGroupName = meshGFF.getString(kGFF4MMHMeshGroupName);
	Common::UString materialName  = meshGFF.getString(kGFF4MMHMaterialObject);
	if (meshGroupName.empty() || materialName.empty())
		return;

	const GFF4Struct *meshChunk = findMeshChunk(*ctx.mshTop, meshGroupName);
	if (!meshChunk)
		return;

	sanityCheckMeshChunk(*meshChunk);

	Common::SeekableReadStream *indexData  = ctx.mshTop->getData(kGFF4MeshIndexData);
	Common::SeekableReadStream *vertexData = ctx.mshTop->getData(kGFF4MeshVertexData);
	if (!indexData || !vertexData)
		return;

	// Read the mesh indices and vertices, and create our buffers

	MeshDeclarations meshDecl;
	readMeshDecl(*meshChunk, meshDecl);
	if (meshDecl.empty())
		return;

	createIndexBuffer (*meshChunk, *indexData);
	createVertexBuffer(*meshChunk, *vertexData, meshDecl);

	_render = true;

	// Load the material object, grab the diffuse texture and load

	MaterialObject materialObject;
	readMAO(materialName, materialObject);

	std::vector<Common::UString> textures;
	textures.push_back(materialObject.textures["mml_tDiffuse"]);

	while (!textures.empty() && textures.back().empty())
		textures.pop_back();

	loadTextures(textures);
}

} // End of namespace Aurora

} // End of namespace Graphics
