/*
---------------------------------------------------------------------------
Open Asset Import Library (ASSIMP)
---------------------------------------------------------------------------

Copyright (c) 2006-2008, ASSIMP Development Team

All rights reserved.

Redistribution and use of this software in source and binary forms, 
with or without modification, are permitted provided that the following 
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the ASSIMP team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the ASSIMP Development Team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file Implementation of the ASE parser class */

#include "ASELoader.h"
#include "MaterialSystem.h"
#include "DefaultLogger.h"
#include "fast_atof.h"

#include "../include/IOStream.h"
#include "../include/IOSystem.h"
#include "../include/aiMesh.h"
#include "../include/aiScene.h"
#include "../include/aiAssert.h"

#include <boost/scoped_ptr.hpp>

using namespace Assimp;
using namespace Assimp::ASE;

#if (defined BLUBB)
#	undef BLUBB
#endif
#define BLUBB(_message_) \
	{this->LogError(_message_);return;}

// ------------------------------------------------------------------------------------------------
Parser::Parser (const char* szFile)
{
	ai_assert(NULL != szFile);
	this->m_szFile = szFile;

	// makre sure that the color values are invalid
	this->m_clrBackground.r = std::numeric_limits<float>::quiet_NaN();
	this->m_clrAmbient.r = std::numeric_limits<float>::quiet_NaN();

	this->iLineNumber = 0;
}
// ------------------------------------------------------------------------------------------------
void Parser::LogWarning(const char* szWarn)
{
	ai_assert(NULL != szWarn);
	ai_assert(strlen(szWarn) < 950);

	char szTemp[1024];
	sprintf(szTemp,"Line %i: %s",this->iLineNumber,szWarn);

	// output the warning to the logger ...
	DefaultLogger::get()->warn(szTemp);
}
// ------------------------------------------------------------------------------------------------
void Parser::LogError(const char* szWarn)
{
	ai_assert(NULL != szWarn);
	ai_assert(strlen(szWarn) < 950);

	char szTemp[1024];
	sprintf(szTemp,"Line %i: %s",this->iLineNumber,szWarn);

	// throw an exception
	throw new ImportErrorException(szTemp);
}
// ------------------------------------------------------------------------------------------------
bool Parser::SkipToNextToken()
{
	while (true)
	{
		if ('*' == *this->m_szFile)return true;
		if ('\0' == *this->m_szFile)return false;

		++this->m_szFile;
	}
}
// ------------------------------------------------------------------------------------------------
bool Parser::SkipOpeningBracket()
{
	if (!SkipSpaces(this->m_szFile,&this->m_szFile))return false;
	if ('{' != *this->m_szFile)
	{
		this->LogWarning("Unable to parse block: Unexpected character, \'{\' expected [#1]");
		return false;
	}
	this->SkipToNextToken();
	return true;
}
// ------------------------------------------------------------------------------------------------
bool Parser::SkipSection()
{
	// must handle subsections ...
	unsigned int iCnt = 1;
	while (true)
	{
		if ('}' == *this->m_szFile)
		{
			--iCnt;
			if (0 == iCnt)
			{
				// go to the next valid token ...
				++this->m_szFile;
				this->SkipToNextToken();
				return true;
			}
		}
		else if ('{' == *this->m_szFile)
		{
			++iCnt;
		}
		else if ('\0' == *this->m_szFile)
		{
			this->LogWarning("Unable to parse block: Unexpected EOF, closing bracket \'}\' was expected [#1]");	
			return false;
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
}
// ------------------------------------------------------------------------------------------------
void Parser::Parse()
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// version should be 200. Validate this ...
			if (0 == strncmp(this->m_szFile,"*3DSMAX_ASCIIEXPORT",19) &&
				IsSpaceOrNewLine(*(this->m_szFile+19)))
			{
				this->m_szFile+=20;

				unsigned int iVersion;
				this->ParseLV4MeshLong(iVersion);

				if (200 != iVersion)
				{
					this->LogWarning("Unknown file format version: *3DSMAX_ASCIIEXPORT should \
						be 200. Continuing happily ...");
				}
			}
			// main scene information
			else if (0 == strncmp(this->m_szFile,"*SCENE",6) &&
				IsSpaceOrNewLine(*(this->m_szFile+6)))
			{
				this->m_szFile+=7;
				this->ParseLV1SceneBlock();
			}
			// material list
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_LIST",14) &&
				IsSpaceOrNewLine(*(this->m_szFile+14)))
			{
				this->m_szFile+=15;
				this->ParseLV1MaterialListBlock();
			}
			// geometric object (mesh)
			else if (0 == strncmp(this->m_szFile,"*GEOMOBJECT",11) &&
				IsSpaceOrNewLine(*(this->m_szFile+11)))
			{
				this->m_szFile+=12;

				this->m_vMeshes.push_back(Mesh());
				this->ParseLV1GeometryObjectBlock(this->m_vMeshes.back());
			}
			// ignore comments, lights and cameras
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... why not?
			return;
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV1SceneBlock()
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			if (0 == strncmp(this->m_szFile,"*SCENE_BACKGROUND_STATIC",24) &&
				IsSpaceOrNewLine(*(this->m_szFile+24)))
			{
				this->m_szFile+=25;

				// parse a color triple and assume it is really the bg color
				this->ParseLV4MeshFloatTriple( &this->m_clrBackground.r );
			}
			else if (0 == strncmp(this->m_szFile,"*SCENE_AMBIENT_STATIC",21) &&
				IsSpaceOrNewLine(*(this->m_szFile+21)))
			{
				this->m_szFile+=22;

				// parse a color triple and assume it is really the bg color
				this->ParseLV4MeshFloatTriple( &this->m_clrAmbient.r );
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... why not?
			return;
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV1MaterialListBlock()
{
	unsigned int iDepth = 1;
	unsigned int iMaterialCount = 0;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			if (0 == strncmp(this->m_szFile,"*MATERIAL_COUNT",15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->ParseLV4MeshLong(iMaterialCount);

				// now allocate enough storage to hold all materials
				this->m_vMaterials.resize(iMaterialCount);
			}
			else if (0 == strncmp(this->m_szFile,"*MATERIAL",9) &&
				IsSpaceOrNewLine(*(this->m_szFile+9)))
			{
				this->m_szFile+=10;
				unsigned int iIndex = 0;
				this->ParseLV4MeshLong(iIndex);

				if (iIndex >= iMaterialCount)
				{
					this->LogWarning("Out of range: material index is too large");
					iIndex = iMaterialCount-1;
				}

				// get a reference to the material
				Material& sMat = this->m_vMaterials[iIndex];

				// skip the '{'
				this->SkipOpeningBracket();

				// parse the material block
				this->ParseLV2MaterialBlock(sMat);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... why not?
			return;
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV2MaterialBlock(ASE::Material& mat)
{
	unsigned int iDepth = 1;
	unsigned int iNumSubMaterials = 0;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			if (0 == strncmp(this->m_szFile,"*MATERIAL_NAME",14) &&
				IsSpaceOrNewLine(*(this->m_szFile+14)))
			{
				this->m_szFile+=15;
			
				// NOTE: The name could also be the texture in some cases
				// be prepared that this might occur ...
				if (!SkipSpaces(this->m_szFile,&this->m_szFile))
					BLUBB("Unable to parse *MATERIAL_NAME block: Unexpected EOL")

				const char* sz = this->m_szFile;
				while (!IsSpaceOrNewLine(*sz))sz++;
				mat.mName = std::string(this->m_szFile,(uintptr_t)sz-(uintptr_t)this->m_szFile);
				this->m_szFile = sz;
			}
			// ambient material color
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_AMBIENT",17) &&
				IsSpaceOrNewLine(*(this->m_szFile+17)))
			{
				this->m_szFile+=18;
				this->ParseLV4MeshFloatTriple(&mat.mAmbient.r);
			}
			// diffuse material color
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_DIFFUSE",17) &&
				IsSpaceOrNewLine(*(this->m_szFile+17)))
			{
				this->m_szFile+=18;
				this->ParseLV4MeshFloatTriple(&mat.mDiffuse.r);
			}
			// specular material color
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_SPECULAR",18) &&
				IsSpaceOrNewLine(*(this->m_szFile+18)))
			{
				this->m_szFile+=19;
				this->ParseLV4MeshFloatTriple(&mat.mSpecular.r);
			}
			// material shading type
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_SHADING",17) &&
				IsSpaceOrNewLine(*(this->m_szFile+17)))
			{
				this->m_szFile+=18;
				
				if (0 == strncmp(this->m_szFile,"Blinn",5) && 
					IsSpaceOrNewLine(*(this->m_szFile+5)))
				{
					mat.mShading = Dot3DSFile::Blinn;
					this->m_szFile+=6;
				}
				else if (0 == strncmp(this->m_szFile,"Phong",5) && 
					IsSpaceOrNewLine(*(this->m_szFile+5)))
				{
					mat.mShading = Dot3DSFile::Phong;
					this->m_szFile+=6;
				}
				else if (0 == strncmp(this->m_szFile,"Flat",4) && 
					IsSpaceOrNewLine(*(this->m_szFile+4)))
				{
					mat.mShading = Dot3DSFile::Flat;
					this->m_szFile+=5;
				}
				else if (0 == strncmp(this->m_szFile,"Wire",4) && 
					IsSpaceOrNewLine(*(this->m_szFile+4)))
				{
					mat.mShading = Dot3DSFile::Wire;
					this->m_szFile+=5;
				}
				else
				{
					// assume gouraud shading
					mat.mShading = Dot3DSFile::Gouraud;
					this->SkipToNextToken();
				}
			}
			// material transparency
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_TRANSPARENCY",22) &&
				IsSpaceOrNewLine(*(this->m_szFile+22)))
			{
				this->m_szFile+=23;
				this->ParseLV4MeshFloat(mat.mTransparency);
				mat.mTransparency = 1.0f - mat.mTransparency;
			}
			// material self illumination
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_SELFILLUM",19) &&
				IsSpaceOrNewLine(*(this->m_szFile+19)))
			{
				this->m_szFile+=20;
				float f = 0.0f;
				this->ParseLV4MeshFloat(f);

				mat.mEmissive.r = f;
				mat.mEmissive.g = f;
				mat.mEmissive.b = f;
			}
			// material shininess
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_SHINE",15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->ParseLV4MeshFloat(mat.mSpecularExponent);
				mat.mSpecularExponent *= 15;
			}
			// diffuse color map
			else if (0 == strncmp(this->m_szFile,"*MAP_DIFFUSE",12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexDiffuse);
			}
			// ambient color map
			else if (0 == strncmp(this->m_szFile,"*MAP_AMBIENT",12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexAmbient);
			}
			// specular color map
			else if (0 == strncmp(this->m_szFile,"*MAP_SPECULAR",13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexSpecular);
			}
			// opacity map
			else if (0 == strncmp(this->m_szFile,"*MAP_OPACITY",12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexOpacity);
			}
			// emissive map
			else if (0 == strncmp(this->m_szFile,"*MAP_SELFILLUM",14) &&
				IsSpaceOrNewLine(*(this->m_szFile+14)))
			{
				this->m_szFile+=15;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexEmissive);
			}
			// bump map
			else if (0 == strncmp(this->m_szFile,"*MAP_BUMP",9) &&
				IsSpaceOrNewLine(*(this->m_szFile+9)))
			{
				this->m_szFile+=10;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexBump);
			}
			// specular/shininess map
			else if (0 == strncmp(this->m_szFile,"*MAP_SHINE",10) &&
				IsSpaceOrNewLine(*(this->m_szFile+10)))
			{
				this->m_szFile+=11;
				// skip the opening bracket
				this->SkipOpeningBracket();
				// parse the texture block
				this->ParseLV3MapBlock(mat.sTexShininess);
			}
			// number of submaterials
			else if (0 == strncmp(this->m_szFile,"*NUMSUBMTLS",11) &&
				IsSpaceOrNewLine(*(this->m_szFile+11)))
			{
				this->m_szFile+=12;
				this->ParseLV4MeshLong(iNumSubMaterials);

				// allocate enough storage
				mat.avSubMaterials.resize(iNumSubMaterials);
			}
			// submaterial chunks
			else if (0 == strncmp(this->m_szFile,"*SUBMATERIAL",12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;
				
				unsigned int iIndex = 0;
				this->ParseLV4MeshLong(iIndex);

				if (iIndex >= iNumSubMaterials)
				{
					this->LogWarning("Out of range: submaterial index is too large");
					iIndex = iNumSubMaterials-1;
				}

				// get a reference to the material
				Material& sMat = mat.avSubMaterials[iIndex];

				// skip the '{'
				this->SkipOpeningBracket();

				// parse the material block
				this->ParseLV2MaterialBlock(sMat);
			}

		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level2 block, this can't be
			BLUBB("Unable to finish parsing a lv2 material block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MapBlock(Texture& map)
{
	unsigned int iDepth = 1;
	unsigned int iNumSubMaterials = 0;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// path to the texture
			if (0 == strncmp(this->m_szFile,"*BITMAP" ,7) &&
				IsSpaceOrNewLine(*(this->m_szFile+7)))
			{
				this->m_szFile+=8;

				// NOTE: The name could also be the texture in some cases
				// be prepared that this might occur ...
				if (!SkipSpaces(this->m_szFile,&this->m_szFile))
					BLUBB("Unable to parse *BITMAP block: Unexpected EOL")

				// there must be "
				if ('\"' != *this->m_szFile)
					BLUBB("Unable to parse *BITMAP block: Path is expected to be enclosed in double quotation marks")

				++this->m_szFile;
				const char* sz = this->m_szFile;
				while (true)
				{
					if ('\"' == *sz)break;
					else if ('\0' == sz)
					{
						BLUBB("Unable to parse *BITMAP block: Path is expected to be enclosed in double quotation marks \
							  but EOF was reached before a closing quotation mark was found")
					}
					sz++;
				}

				map.mMapName = std::string(this->m_szFile,(uintptr_t)sz-(uintptr_t)this->m_szFile);
				this->m_szFile = sz;
			}
			// offset on the u axis
			if (0 == strncmp(this->m_szFile,"*UVW_U_OFFSET" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				this->ParseLV4MeshFloat(map.mOffsetU);
			}
			// offset on the v axis
			if (0 == strncmp(this->m_szFile,"*UVW_V_OFFSET" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				this->ParseLV4MeshFloat(map.mOffsetV);
			}
			// tiling on the u axis
			if (0 == strncmp(this->m_szFile,"*UVW_U_TILING" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				this->ParseLV4MeshFloat(map.mScaleU);
			}
			// tiling on the v axis
			if (0 == strncmp(this->m_szFile,"*UVW_V_TILING" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				this->ParseLV4MeshFloat(map.mScaleV);
			}
			// rotation around the z-axis
			if (0 == strncmp(this->m_szFile,"*UVW_ANGLE" ,10) &&
				IsSpaceOrNewLine(*(this->m_szFile+10)))
			{
				this->m_szFile+=11;
				this->ParseLV4MeshFloat(map.mRotation);
			}
			// map blending factor
			if (0 == strncmp(this->m_szFile,"*MAP_AMOUNT" ,11) &&
				IsSpaceOrNewLine(*(this->m_szFile+11)))
			{
				this->m_szFile+=12;
				this->ParseLV4MeshFloat(map.mTextureBlend);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing a lv3 map block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV1GeometryObjectBlock(ASE::Mesh& mesh)
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// name of the mesh/node
			if (0 == strncmp(this->m_szFile,"*NODE_NAME" ,10) &&
				IsSpaceOrNewLine(*(this->m_szFile+10)))
			{
				this->m_szFile+=11;

				// NOTE: The name could also be the texture in some cases
				// be prepared that this might occur ...
				if (!SkipSpaces(this->m_szFile,&this->m_szFile))
					BLUBB("Unable to parse *NODE_NAME block: Unexpected EOL")

				// there must be "
				if ('\"' != *this->m_szFile)
					BLUBB("Unable to parse *NODE_NAME block: Name is expected to be enclosed in double quotation marks")

				++this->m_szFile;
				const char* sz = this->m_szFile;
				while (true)
				{
					if ('\"' == *sz)break;
					else if ('\0' == sz)
					{
						BLUBB("Unable to parse *NODE_NAME block: Name is expected to be enclosed in double quotation marks \
							  but EOF was reached before a closing quotation mark was found")
					}
					sz++;
				}

				mesh.mName = std::string(this->m_szFile,(uintptr_t)sz-(uintptr_t)this->m_szFile);
				this->m_szFile = sz;
			}
			// transformation matrix of the node
			else if (0 == strncmp(this->m_szFile,"*NODE_TM" ,8) &&
				IsSpaceOrNewLine(*(this->m_szFile+8)))
			{
				this->m_szFile+=9;
				this->ParseLV2NodeTransformBlock(mesh);
			}
			// mesh data
			else if (0 == strncmp(this->m_szFile,"*MESH" ,5) &&
				IsSpaceOrNewLine(*(this->m_szFile+5)))
			{
				this->m_szFile+=6;
				this->ParseLV2MeshBlock(mesh);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level1 block, this can be
			return;
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV2NodeTransformBlock(ASE::Mesh& mesh)
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// first row of the transformation matrix
			if (0 == strncmp(this->m_szFile,"*TM_ROW0" ,8) &&
				IsSpaceOrNewLine(*(this->m_szFile+8)))
			{
				this->m_szFile+=9;
				this->ParseLV4MeshFloatTriple(mesh.mTransform[0]);
			}
			// second row of the transformation matrix
			else if (0 == strncmp(this->m_szFile,"*TM_ROW1" ,8) &&
				IsSpaceOrNewLine(*(this->m_szFile+8)))
			{
				this->m_szFile+=9;
				this->ParseLV4MeshFloatTriple(mesh.mTransform[1]);
			}
			// third row of the transformation matrix
			else if (0 == strncmp(this->m_szFile,"*TM_ROW2" ,8) &&
				IsSpaceOrNewLine(*(this->m_szFile+8)))
			{
				this->m_szFile+=9;
				this->ParseLV4MeshFloatTriple(mesh.mTransform[2]);
			}
			// fourth row of the transformation matrix
			else if (0 == strncmp(this->m_szFile,"*TM_ROW3" ,8) &&
				IsSpaceOrNewLine(*(this->m_szFile+8)))
			{
				this->m_szFile+=9;
				this->ParseLV4MeshFloatTriple(mesh.mTransform[3]);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level2 block, this can't be
			BLUBB("Unable to finish parsing a lv2 node transform block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV2MeshBlock(ASE::Mesh& mesh)
{
	unsigned int iNumVertices = 0;
	unsigned int iNumFaces = 0;
	unsigned int iNumTVertices = 0;
	unsigned int iNumTFaces = 0;
	unsigned int iNumCVertices = 0;
	unsigned int iNumCFaces = 0;
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Number of vertices in the mesh
			if (0 == strncmp(this->m_szFile,"*MESH_NUMVERTEX" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->ParseLV4MeshLong(iNumVertices);
			}
			// Number of texture coordinates in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMTVERTEX" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumTVertices);
			}
			// Number of vertex colors in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMCVERTEX" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumCVertices);
			}
			// Number of regular faces in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMFACES" ,14) &&
				IsSpaceOrNewLine(*(this->m_szFile+14)))
			{
				this->m_szFile+=15;
				this->ParseLV4MeshLong(iNumFaces);
			}
			// Number of UVWed faces in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMTVFACES" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumTFaces);
			}
			// Number of colored faces in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMCVFACES" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumCFaces);
			}
			// mesh vertex list block
			else if (0 == strncmp(this->m_szFile,"*MESH_VERTEX_LIST" ,17) &&
				IsSpaceOrNewLine(*(this->m_szFile+17)))
			{
				this->m_szFile+=18;
				this->ParseLV3MeshVertexListBlock(iNumVertices,mesh);
			}
			// mesh face list block
			else if (0 == strncmp(this->m_szFile,"*MESH_FACE_LIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshFaceListBlock(iNumFaces,mesh);
			}
			// mesh texture vertex list block
			else if (0 == strncmp(this->m_szFile,"*MESH_TVERTLIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshTListBlock(iNumTVertices,mesh);
			}
			// mesh texture face block
			else if (0 == strncmp(this->m_szFile,"*MESH_TFACELIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshTFaceListBlock(iNumTFaces,mesh);
			}
			// mesh color vertex list block
			else if (0 == strncmp(this->m_szFile,"*MESH_CVERTLIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshCListBlock(iNumCVertices,mesh);
			}
			// mesh color face block
			else if (0 == strncmp(this->m_szFile,"*MESH_CFACELIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshCFaceListBlock(iNumCFaces,mesh);
			}
			// another mesh UV channel ...
			else if (0 == strncmp(this->m_szFile,"*MESH_MAPPINGCHANNEL" ,20) &&
				IsSpaceOrNewLine(*(this->m_szFile+20)))
			{
				this->m_szFile+=21;

				unsigned int iIndex = 0;
				this->ParseLV4MeshLong(iIndex);

				if (iIndex < 2)
				{
					this->LogWarning("Mapping channel has an invalid index. Skipping UV channel");
					// skip it ...
					this->SkipOpeningBracket();
					this->SkipSection();
				}
				if (iIndex > AI_MAX_NUMBER_OF_TEXTURECOORDS)
				{
					this->LogWarning("Too many UV channels specified. Skipping channel ..");
					// skip it ...
					this->SkipOpeningBracket();
					this->SkipSection();
				}
				else
				{
					// skip the '{'
					this->SkipOpeningBracket();

					// parse the mapping channel
					this->ParseLV3MappingChannel(iIndex-1,mesh);
				}
			}
			// mesh material index
			else if (0 == strncmp(this->m_szFile,"*MATERIAL_REF" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;
				this->ParseLV4MeshLong(mesh.iMaterialIndex);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level2 block, this can't be
			BLUBB("Unable to finish parsing a lv2 mesh block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshVertexListBlock(
	unsigned int iNumVertices, ASE::Mesh& mesh)
{
	// allocate enough storage in the array
	mesh.mPositions.resize(iNumVertices);
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Vertex entry
			if (0 == strncmp(this->m_szFile,"*MESH_VERTEX" ,12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;

				aiVector3D vTemp;
				unsigned int iIndex;
				this->ParseLV4MeshFloatTriple(&vTemp.x,iIndex);

				if (iIndex >= iNumVertices)
				{
					this->LogWarning("Vertex has an invalid index. It will be ignored");
				}
				else mesh.mPositions[iIndex] = vTemp;
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing a lv3 vertex list block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshFaceListBlock(unsigned int iNumFaces, ASE::Mesh& mesh)
{
	// allocate enough storage in the face array
	mesh.mFaces.resize(iNumFaces);
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Face entry
			if (0 == strncmp(this->m_szFile,"*MESH_FACE" ,10) &&
				IsSpaceOrNewLine(*(this->m_szFile+10)))
			{
				this->m_szFile+=11;

				ASE::Face mFace;
				this->ParseLV4MeshFace(mFace);

				if (mFace.iFace >= iNumFaces)
				{
					this->LogWarning("Face has an invalid index. It will be ignored");
				}
				else mesh.mFaces[mFace.iFace] = mFace;
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing LV3 *MESH_FACE_LIST block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshTListBlock(unsigned int iNumVertices,
	ASE::Mesh& mesh, unsigned int iChannel)
{
	// allocate enough storage in the array
	mesh.amTexCoords[iChannel].resize(iNumVertices);
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Vertex entry
			if (0 == strncmp(this->m_szFile,"*MESH_TVERT" ,11) &&
				IsSpaceOrNewLine(*(this->m_szFile+11)))
			{
				this->m_szFile+=12;

				aiVector3D vTemp;
				unsigned int iIndex;
				this->ParseLV4MeshFloatTriple(&vTemp.x,iIndex);

				if (iIndex >= iNumVertices)
				{
					this->LogWarning("Tvertex has an invalid index. It will be ignored");
				}
				else mesh.amTexCoords[iChannel][iIndex] = vTemp;

				if (0.0f != vTemp.z)
				{
					// we need 3 coordinate channels
					mesh.mNumUVComponents[iChannel] = 3;
				}
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing LV3 *MESH_VERTEX_LIST block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshTFaceListBlock(unsigned int iNumFaces,
	ASE::Mesh& mesh, unsigned int iChannel)
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Face entry
			if (0 == strncmp(this->m_szFile,"*MESH_TFACE" ,12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;

				unsigned int aiValues[3];
				unsigned int iIndex = 0;

				this->ParseLV4MeshLongTriple(aiValues,iIndex);
				if (iIndex >= iNumFaces || iIndex >= mesh.mFaces.size())
				{
					this->LogWarning("UV-Face has an invalid index. It will be ignored");
				}
				else
				{
					// copy UV indices
					mesh.mFaces[iIndex].amUVIndices[iChannel][0] = aiValues[0];
					mesh.mFaces[iIndex].amUVIndices[iChannel][1] = aiValues[1];
					mesh.mFaces[iIndex].amUVIndices[iChannel][2] = aiValues[2];
				}
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing LV3 *MESH_TFACELIST block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MappingChannel(unsigned int iChannel, ASE::Mesh& mesh)
{
	unsigned int iNumTVertices = 0;
	unsigned int iNumTFaces = 0;

	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Number of texture coordinates in the mesh
			if (0 == strncmp(this->m_szFile,"*MESH_NUMTVERTEX" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumTVertices);
			}
			// Number of UVWed faces in the mesh
			else if (0 == strncmp(this->m_szFile,"*MESH_NUMTVFACES" ,16) &&
				IsSpaceOrNewLine(*(this->m_szFile+16)))
			{
				this->m_szFile+=17;
				this->ParseLV4MeshLong(iNumTFaces);
			}
			// mesh texture vertex list block
			else if (0 == strncmp(this->m_szFile,"*MESH_TVERTLIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshTListBlock(iNumTVertices,mesh,iChannel);
			}
			// mesh texture face block
			else if (0 == strncmp(this->m_szFile,"*MESH_TFACELIST" ,15) &&
				IsSpaceOrNewLine(*(this->m_szFile+15)))
			{
				this->m_szFile+=16;
				this->SkipOpeningBracket();
				this->ParseLV3MeshTFaceListBlock(iNumTFaces,mesh, iChannel);
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level2 block, this can't be
			BLUBB("Unable to finish parsing a LV3 *MESH_MAPPINGCHANNEL block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshCListBlock(unsigned int iNumVertices, ASE::Mesh& mesh)
{
	// allocate enough storage in the array
	mesh.mVertexColors.resize(iNumVertices);
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Vertex entry
			if (0 == strncmp(this->m_szFile,"*MESH_VERTCOL" ,13) &&
				IsSpaceOrNewLine(*(this->m_szFile+13)))
			{
				this->m_szFile+=14;

				aiColor4D vTemp;
				vTemp.a = 1.0f;
				unsigned int iIndex;
				this->ParseLV4MeshFloatTriple(&vTemp.r,iIndex);

				if (iIndex >= iNumVertices)
				{
					this->LogWarning("Vertex color has an invalid index. It will be ignored");
				}
				else mesh.mVertexColors[iIndex] = vTemp;
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing LV3 *MESH_CVERTLIST block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshCFaceListBlock(unsigned int iNumFaces, ASE::Mesh& mesh)
{
	unsigned int iDepth = 1;
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			// Face entry
			if (0 == strncmp(this->m_szFile,"*MESH_CFACE" ,12) &&
				IsSpaceOrNewLine(*(this->m_szFile+12)))
			{
				this->m_szFile+=13;

				unsigned int aiValues[3];
				unsigned int iIndex = 0;

				this->ParseLV4MeshLongTriple(aiValues,iIndex);
				if (iIndex >= iNumFaces || iIndex >= mesh.mFaces.size())
				{
					this->LogWarning("UV-Face has an invalid index. It will be ignored");
				}
				else
				{
					// copy color indices
					mesh.mFaces[iIndex].mColorIndices[0] = aiValues[0];
					mesh.mFaces[iIndex].mColorIndices[1] = aiValues[1];
					mesh.mFaces[iIndex].mColorIndices[2] = aiValues[2];
				}
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		else if ('\0' == *this->m_szFile)
		{
			// END OF FILE ... this is a level3 block, this can't be
			BLUBB("Unable to finish parsing LV3 *MESH_CFACELIST block. Unexpected EOF")
		}
		else if(IsLineEnd(*this->m_szFile))++this->iLineNumber;
		++this->m_szFile;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV3MeshNormalListBlock(ASE::Mesh& sMesh)
{
	// allocate enough storage for the normals
	sMesh.mNormals.resize(sMesh.mPositions.size());
	unsigned int iDepth = 1;

	// we need the *MESH_VERTEXNORMAL blocks, ignore the face normals
	// if there are only face normals we calculate them outselfes using the SGs
	while (true)
	{
		if ('*' == *this->m_szFile)
		{
			if (0 == strncmp(this->m_szFile,"*MESH_VERTEXNORMAL",18) && IsSpaceOrNewLine(*(this->m_szFile+18)))
			{
				this->m_szFile += 19;

				// parse a simple float triple
				aiVector3D vNormal;
				unsigned int iIndex = 0;
				this->ParseLV4MeshFloatTriple(&vNormal.x,iIndex);

				if (iIndex >= sMesh.mNormals.size())
				{
					this->LogWarning("Normal index is too large");
					iIndex = sMesh.mNormals.size()-1;
				}

				// important: this->m_szFile might now point to '}' ...
				sMesh.mNormals[iIndex] = vNormal;
			}
		}
		if ('{' == *this->m_szFile)iDepth++;
		if ('}' == *this->m_szFile)
		{
			if (0 == --iDepth)
			{
				++this->m_szFile;
				this->SkipToNextToken();
				return;
			}
		}
		// seems we have reached the end of the file ... 
		else if ('\0' == *this->m_szFile)
		{
			BLUBB("Unable to parse *MESH_NORMALS Element: Unexpected EOL [#1]")
		}
		this->m_szFile++;
	}
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshFace(ASE::Face& out)
{
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
		BLUBB("Unable to parse *MESH_FACE Element: Unexpected EOL [#1]")

	// parse the face index
	out.iFace = strtol10(this->m_szFile,&this->m_szFile);

	// next character should be ':'
	if(!SkipSpaces(this->m_szFile,&this->m_szFile) || ':' != *this->m_szFile)
		BLUBB("Unable to parse *MESH_FACE Element: Unexpected EOL. \':\' expected [#2]")

	// parse all mesh indices
	++this->m_szFile;
	for (unsigned int i = 0; i < 3;++i)
	{
		unsigned int iIndex = 0;
		if(!SkipSpaces(this->m_szFile,&this->m_szFile))
		{
			// LOG 
__EARTHQUAKE_XXL:
			BLUBB("Unable to parse *MESH_FACE Element: Unexpected EOL. A,B or C expected [#3]")
		}
		switch (*this->m_szFile)
		{
		case 'A':
		case 'a':
			break;
		case 'B':
		case 'b':
			iIndex = 1;
			break;
		case 'C':
		case 'c':
			iIndex = 2;
			break;
		default: goto __EARTHQUAKE_XXL;
		};
		++this->m_szFile;

		// next character should be ':'
		if(!SkipSpaces(this->m_szFile,&this->m_szFile) || ':' != *this->m_szFile)
			BLUBB("Unable to parse *MESH_FACE Element: Unexpected EOL. \':\' expected [#2]")

		++this->m_szFile;

		if(!SkipSpaces(this->m_szFile,&this->m_szFile))
			BLUBB("Unable to parse *MESH_FACE Element: Unexpected EOL. Vertex index ecpected [#4]")

		out.mIndices[iIndex] = strtol10(this->m_szFile,&this->m_szFile);
	}

	// now we need to skip the AB, BC, CA blocks. 
	while (true)
	{
		if ('*' == *this->m_szFile)break;
		if (IsLineEnd(*this->m_szFile))
		{
			this->iLineNumber++;
			return;
		}
		this->m_szFile++;
	}

	// parse the smoothing group of the face
	if (0 == strncmp(this->m_szFile,"*MESH_SMOOTHING",15) && IsSpaceOrNewLine(*(this->m_szFile+15)))
	{
		this->m_szFile+=16;
		if(!SkipSpaces(this->m_szFile,&this->m_szFile))
			BLUBB("Unable to parse *MESH_SMOOTHING Element: Unexpected EOL. Smoothing group(s) expected [#5]")
		
		// parse smoothing groups until we don_t anymore see commas
		while (true)
		{
			out.iSmoothGroup |= (1 << strtol10(this->m_szFile,&this->m_szFile));
			SkipSpaces(this->m_szFile,&this->m_szFile);
			if (',' != *this->m_szFile)
			{
				break;
			}
			++this->m_szFile;
			SkipSpaces(this->m_szFile,&this->m_szFile);
		}
	}

	// *MESH_MTLID  is optional, too
	while (true)
	{
		if ('*' == *this->m_szFile)break;
		if (IsLineEnd(*this->m_szFile))
		{
			this->iLineNumber++;
			return;
		}
		this->m_szFile++;
	}

	if (0 == strncmp(this->m_szFile,"*MESH_MTLID",11) && IsSpaceOrNewLine(*(this->m_szFile+11)))
	{
		this->m_szFile+=12;
		if(!SkipSpaces(this->m_szFile,&this->m_szFile))
			BLUBB("Unable to parse *MESH_MTLID Element: Unexpected EOL. Material index expected [#6]")
		out.iMaterial = strtol10(this->m_szFile,&this->m_szFile);
	}
	this->SkipToNextToken();
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshLongTriple(unsigned int* apOut)
{
	ai_assert(NULL != apOut);

	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse indexable long triple: unexpected EOL [#1]");
		++this->iLineNumber;
		apOut[0] = apOut[1] = apOut[2] = 0;
		return;
	}
	apOut[0] = strtol10(this->m_szFile,&this->m_szFile);

	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse indexable long triple: unexpected EOL [#2]");
		++this->iLineNumber;
		apOut[1] = apOut[2] = 0;
		return;
	}
	apOut[1] = strtol10(this->m_szFile,&this->m_szFile);

	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse indexable long triple: unexpected EOL [#3]");
		apOut[2] = 0;
		++this->iLineNumber;
		return;
	}
	apOut[2] = strtol10(this->m_szFile,&this->m_szFile);
	// go to the next valid sequence
	SkipSpacesAndLineEnd(this->m_szFile,&this->m_szFile);
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshLongTriple(unsigned int* apOut, unsigned int& rIndexOut)
{
	ai_assert(NULL != apOut);

	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse indexable long triple: unexpected EOL [#4]");
		rIndexOut = 0;
		apOut[0] = apOut[1] = apOut[2] = 0;
		++this->iLineNumber;
		return;
	}
	// parse the index
	rIndexOut = strtol10(this->m_szFile,&this->m_szFile);

	// parse the three others
	this->ParseLV4MeshLongTriple(apOut);
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshFloatTriple(float* apOut, unsigned int& rIndexOut)
{
	ai_assert(NULL != apOut);

	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse indexable float triple: unexpected EOL [#1]");
		rIndexOut = 0;
		apOut[0] = apOut[1] = apOut[2] = 0.0f;
		++this->iLineNumber;
		return;
	}
	// parse the index
	rIndexOut = strtol10(this->m_szFile,&this->m_szFile);
	
	// parse the three others
	this->ParseLV4MeshFloatTriple(apOut);
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshFloatTriple(float* apOut)
{
	ai_assert(NULL != apOut);
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse float triple: unexpected EOL [#5]");
		apOut[0] = apOut[1] = apOut[2] = 0.0f;
		++this->iLineNumber;
		return;
	}
	// parse the first float
	this->m_szFile = fast_atof_move(this->m_szFile,apOut[0]);
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse float triple: unexpected EOL [#6]");
		apOut[1] = apOut[2] = 0.0f;
		++this->iLineNumber;
		return;
	}
	// parse the second float
	this->m_szFile = fast_atof_move(this->m_szFile,apOut[1]);
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse float triple: unexpected EOL [#7]");
		apOut[2] = 0.0f;
		++this->iLineNumber;
		return;
	}
	// parse the third float
	this->m_szFile = fast_atof_move(this->m_szFile,apOut[2]);
	// go to the next valid sequence
	this->SkipToNextToken();
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshFloat(float& fOut)
{
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse float: unexpected EOL [#1]");
		fOut = 0.0f;
		++this->iLineNumber;
		return;
	}
	// parse the first float
	this->m_szFile = fast_atof_move(this->m_szFile,fOut);
	// go to the next valid sequence
	this->SkipToNextToken();
	return;
}
// ------------------------------------------------------------------------------------------------
void Parser::ParseLV4MeshLong(unsigned int& iOut)
{
	// skip spaces and tabs
	if(!SkipSpaces(this->m_szFile,&this->m_szFile))
	{
		// LOG 
		this->LogWarning("Unable to parse long: unexpected EOL [#1]");
		iOut = 0;
		++this->iLineNumber;
		return;
	}
	// parse the value
	iOut = strtol10(this->m_szFile,&this->m_szFile);
	// go to the next valid sequence
	this->SkipToNextToken();
	return;
}