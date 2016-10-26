/*****************************************************************************************
*                                                                                       *
* OpenSpace                                                                             *
*                                                                                       *
* Copyright (c) 2014-2016                                                               *
*                                                                                       *
* Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
* software and associated documentation files (the "Software"), to deal in the Software *
* without restriction, including without limitation the rights to use, copy, modify,    *
* merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
* permit persons to whom the Software is furnished to do so, subject to the following   *
* conditions:                                                                           *
*                                                                                       *
* The above copyright notice and this permission notice shall be included in all copies *
* or substantial portions of the Software.                                              *
*                                                                                       *
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
* PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
* CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
* OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
****************************************************************************************/


#include <modules/globebrowsing/chunk/chunkrenderer.h>
#include <modules/globebrowsing/globes/chunkedlodglobe.h>
#include <modules/globebrowsing/globes/renderableglobe.h>
#include <modules/globebrowsing/layered_rendering/layeredtextures.h>
#include <modules/globebrowsing/tile/tileprovidermanager.h>

// open space includes
#include <openspace/engine/wrapper/windowwrapper.h>
#include <openspace/engine/openspaceengine.h> 
#include <openspace/rendering/renderengine.h>

// ghoul includes
#include <ghoul/misc/assert.h>
#include <ghoul/opengl/texture.h>
#include <ghoul/opengl/textureunit.h>

// STL includes
#include <sstream> 

#define _USE_MATH_DEFINES
#include <math.h>

namespace {
    const std::string _loggerCat = "ChunkRenderer";

    const std::string keyFrame = "Frame";
    const std::string keyGeometry = "Geometry";
    const std::string keyShading = "PerformShading";

    const std::string keyBody = "Body";
}

namespace openspace {
namespace globebrowsing {

    ChunkRenderer::ChunkRenderer(
        std::shared_ptr<Grid> grid,
        std::shared_ptr<TileProviderManager> tileProviderManager)
        : _tileProviderManager(tileProviderManager)
        , _grid(grid)
    {
        _globalRenderingShaderProvider = std::make_shared<LayeredTextureShaderProvider>(
                "GlobalChunkedLodPatch",
                "${MODULE_GLOBEBROWSING}/shaders/globalchunkedlodpatch_vs.glsl",
                "${MODULE_GLOBEBROWSING}/shaders/globalchunkedlodpatch_fs.glsl");

        _localRenderingShaderProvider = std::make_shared<LayeredTextureShaderProvider>(
                "LocalChunkedLodPatch",
                "${MODULE_GLOBEBROWSING}/shaders/localchunkedlodpatch_vs.glsl",
                "${MODULE_GLOBEBROWSING}/shaders/localchunkedlodpatch_fs.glsl");

        _globalProgramUniformHandler =
            std::make_shared<LayeredTextureShaderUniformIdHandler>();
        _localProgramUniformHandler =
            std::make_shared<LayeredTextureShaderUniformIdHandler>();

    }

    void ChunkRenderer::renderChunk(const Chunk& chunk, const RenderData& data) {
        // A little arbitrary but it works
        if (chunk.tileIndex().level < 10) {
            renderChunkGlobally(chunk, data);
        }
        else {
            renderChunkLocally(chunk, data);
        }
    }

    void ChunkRenderer::update() {
        // unused atm. Could be used for caching or precalculating
    }

    void ChunkRenderer::setDepthTransformUniforms(
        std::shared_ptr<LayeredTextureShaderUniformIdHandler> uniformIdHandler,
        LayeredTextures::TextureCategory textureCategory,
        LayeredTextures::BlendLayerSuffixes blendLayerSuffix,
        size_t layerIndex,
        const TileDepthTransform& tileDepthTransform)
    {   
        uniformIdHandler->programObject().setUniform(
            uniformIdHandler->getId(
                textureCategory,
                blendLayerSuffix,
                layerIndex,
                LayeredTextures::GlslTileDataId::depthTransform_depthScale),
            tileDepthTransform.depthScale);

        uniformIdHandler->programObject().setUniform(
            uniformIdHandler->getId(
                textureCategory,
                blendLayerSuffix,
                layerIndex,
                LayeredTextures::GlslTileDataId::depthTransform_depthOffset),
            tileDepthTransform.depthOffset);

    }

    void ChunkRenderer::activateTileAndSetTileUniforms(
        std::shared_ptr<LayeredTextureShaderUniformIdHandler> uniformIdHandler,
        LayeredTextures::TextureCategory textureCategory,
        LayeredTextures::BlendLayerSuffixes blendLayerSuffix,
        size_t layerIndex,
        ghoul::opengl::TextureUnit& texUnit,
        const ChunkTile& chunkTile)
    {
        // Blend tile with two parents
        // The texture needs a unit to sample from
        texUnit.activate();
        chunkTile.tile.texture->bind();

        uniformIdHandler->programObject().setUniform(
            uniformIdHandler->getId(
                textureCategory,
                blendLayerSuffix,
                layerIndex,
                LayeredTextures::GlslTileDataId::textureSampler),
            texUnit);
        uniformIdHandler->programObject().setUniform(
            uniformIdHandler->getId(
                textureCategory,
                blendLayerSuffix,
                layerIndex,
                LayeredTextures::GlslTileDataId::uvTransform_uvScale),
            chunkTile.uvTransform.uvScale);
        uniformIdHandler->programObject().setUniform(
            uniformIdHandler->getId(
                textureCategory,
                blendLayerSuffix,
                layerIndex,
                LayeredTextures::GlslTileDataId::uvTransform_uvOffset),
            chunkTile.uvTransform.uvOffset);
    }

    void ChunkRenderer::setLayerSettingsUniforms(
        std::shared_ptr<LayeredTextureShaderUniformIdHandler> uniformIdHandler,
        LayeredTextures::TextureCategory textureCategory,
        size_t layerIndex,
        PerLayerSettings settings) {
        
        for (int i = 0; i < settings.array().size(); i++) {
            settings.array()[i]->uploadUniform(
                uniformIdHandler->programObject(),
                uniformIdHandler->getSettingsId(
                    textureCategory,
                    layerIndex,
                    LayeredTextures::LayerSettingsIds(i)));
        }
    }

    ProgramObject* ChunkRenderer::getActivatedProgramWithTileData(
        LayeredTextureShaderProvider* layeredTextureShaderProvider,
        std::shared_ptr<LayeredTextureShaderUniformIdHandler> programUniformHandler,
        const Chunk& chunk)
    {
        const TileIndex& tileIndex = chunk.tileIndex();

        LayeredTexturePreprocessingData layeredTexturePreprocessingData;
        
        for (size_t category = 0;
            category < LayeredTextures::NUM_TEXTURE_CATEGORIES;
            category++) {

            LayeredTextureInfo layeredTextureInfo;
            auto layerGroup = _tileProviderManager->layerGroup(category);
            layeredTextureInfo.lastLayerIdx = layerGroup.activeLayers().size() - 1;
            layeredTextureInfo.layerBlendingEnabled = layerGroup.levelBlendingEnabled;

            layeredTexturePreprocessingData.layeredTextureInfo[category] = layeredTextureInfo;
        }

        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "useAtmosphere",
                std::to_string(chunk.owner().generalProperties().atmosphereEnabled)));
        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "performShading",
                std::to_string(chunk.owner().generalProperties().performShading)));

        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "showChunkEdges",
                std::to_string(chunk.owner().debugProperties().showChunkEdges)));


        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "showHeightResolution",
                std::to_string(chunk.owner().debugProperties().showHeightResolution)));

        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "showHeightIntensities",
                std::to_string(chunk.owner().debugProperties().showHeightIntensities)));

        layeredTexturePreprocessingData.keyValuePairs.push_back(
            std::pair<std::string, std::string>(
                "defaultHeight",
                std::to_string(Chunk::DEFAULT_HEIGHT)));

        // Now the shader program can be accessed
        ProgramObject* programObject =
            layeredTextureShaderProvider->getUpdatedShaderProgram(
                layeredTexturePreprocessingData);

        programUniformHandler->updateIdsIfNecessary(layeredTextureShaderProvider);

        // Activate the shader program
        programObject->activate();

        // Initialize all texture units
        struct BlendTexUnits {
            ghoul::opengl::TextureUnit blendTexture0;
            ghoul::opengl::TextureUnit blendTexture1;
            ghoul::opengl::TextureUnit blendTexture2;
        };
        std::array<std::vector<BlendTexUnits>, LayeredTextures::NUM_TEXTURE_CATEGORIES> texUnits;
        for (size_t category = 0; category < LayeredTextures::NUM_TEXTURE_CATEGORIES; category++) {
            auto layerGroup = _tileProviderManager->layerGroup(category);
            texUnits[category].resize(layerGroup.activeLayers().size());
        }

        // Go through all the categories
        for (size_t category = 0; category < LayeredTextures::NUM_TEXTURE_CATEGORIES; category++) {
            // Go through all the providers in this category
            auto layerGroup = _tileProviderManager->layerGroup(category);
            const auto& layers = layerGroup.activeLayers();
            int i = 0;
            for (auto it = layers.begin(); it != layers.end(); it++) {
                auto tileProvider = it->tileProvider.get();

                // Get the texture that should be used for rendering
                ChunkTile chunkTile = TileSelector::getHighestResolutionTile(tileProvider, tileIndex);
                if (chunkTile.tile.status == Tile::Status::Unavailable) {
                    chunkTile.tile = tileProvider->getDefaultTile();
                    chunkTile.uvTransform.uvOffset = { 0, 0 };
                    chunkTile.uvTransform.uvScale = { 1, 1 };
                }

                activateTileAndSetTileUniforms(
                    programUniformHandler,
                    LayeredTextures::TextureCategory(category),
                    LayeredTextures::BlendLayerSuffixes::none,
                    i,
                    texUnits[category][i].blendTexture0,
                    chunkTile);

                // If blending is enabled, two more textures are needed
                if (layeredTexturePreprocessingData.layeredTextureInfo[category].layerBlendingEnabled) {
                    ChunkTile chunkTileParent1 = TileSelector::getHighestResolutionTile(tileProvider, tileIndex, 1);
                    if (chunkTileParent1.tile.status == Tile::Status::Unavailable) {
                        chunkTileParent1 = chunkTile;
                    }
                    activateTileAndSetTileUniforms(
                        programUniformHandler,
                        LayeredTextures::TextureCategory(category),
                        LayeredTextures::BlendLayerSuffixes::Parent1,
                        i,
                        texUnits[category][i].blendTexture1,
                        chunkTileParent1);

                    ChunkTile chunkTileParent2 = TileSelector::getHighestResolutionTile(tileProvider, tileIndex, 2);
                    if (chunkTileParent2.tile.status == Tile::Status::Unavailable) {
                        chunkTileParent2 = chunkTileParent1;
                    }
                    activateTileAndSetTileUniforms(
                        programUniformHandler,
                        LayeredTextures::TextureCategory(category),
                        LayeredTextures::BlendLayerSuffixes::Parent2,
                        i,
                        texUnits[category][i].blendTexture2,
                        chunkTileParent2);
                }
                
                setLayerSettingsUniforms(
                    programUniformHandler,
                    LayeredTextures::TextureCategory(category),
                    i,
                    _tileProviderManager->layerGroup(category).activeLayers()[i].settings);

                /*
                if (category == LayeredTextures::HeightMaps && chunkTile.tile.preprocessData) {
                    //auto preprocessingData = chunkTile.tile.preprocessData;
                    //float noDataValue = preprocessingData->noDataValues[0];
                    programObject->setUniform(
                        "minimumValidHeight[" + std::to_string(i) + "]",
                        -100000);
                }
                */
                i++;
            }
        }

        // Go through all the height maps and set depth tranforms
        int i = 0;
        auto heightLayers = _tileProviderManager->layerGroup(LayeredTextures::HeightMaps).activeLayers();
        for (auto it = heightLayers.begin(); it != heightLayers.end(); it++) {
            auto tileProvider = it->tileProvider;

            TileDepthTransform depthTransform = tileProvider->depthTransform();
            setDepthTransformUniforms(
                programUniformHandler,
                LayeredTextures::TextureCategory::HeightMaps,
                LayeredTextures::BlendLayerSuffixes::none,
                i,
                depthTransform);
            i++;
        }

        // The length of the skirts is proportional to its size
        programObject->setUniform("skirtLength", min(static_cast<float>(chunk.surfacePatch().halfSize().lat * 1000000), 8700.0f));
        programObject->setUniform("xSegments", _grid->xSegments());
        programObject->setUniform("chunkMinHeight", chunk.getBoundingHeights().min);

        if (chunk.owner().debugProperties().showHeightResolution) {
            programObject->setUniform("vertexResolution", glm::vec2(_grid->xSegments(), _grid->ySegments()));
        }       
        
        return programObject;
    }

    void ChunkRenderer::renderChunkGlobally(const Chunk& chunk, const RenderData& data){

        ProgramObject* programObject = getActivatedProgramWithTileData(
            _globalRenderingShaderProvider.get(),
            _globalProgramUniformHandler,
            chunk);
        if (programObject == nullptr) {
            return;
        }

        const Ellipsoid& ellipsoid = chunk.owner().ellipsoid();

        bool performAnyBlending = false;
        
        for (int i = 0; i < LayeredTextures::NUM_TEXTURE_CATEGORIES; ++i) {
            LayeredTextures::TextureCategory category = (LayeredTextures::TextureCategory)i;
            if(_tileProviderManager->layerGroup(i).levelBlendingEnabled && _tileProviderManager->layerGroup(category).activeLayers().size() > 0){
                performAnyBlending = true; 
                break;
            }
        }
        if (performAnyBlending) {
            // Calculations are done in the reference frame of the globe. Hence, the camera
            // position needs to be transformed with the inverse model matrix
            glm::dmat4 inverseModelTransform = chunk.owner().inverseModelTransform();
            glm::dvec3 cameraPosition =
                glm::dvec3(inverseModelTransform * glm::dvec4(data.camera.positionVec3(), 1));
            float distanceScaleFactor = chunk.owner().generalProperties().lodScaleFactor * ellipsoid.minimumRadius();
            programObject->setUniform("cameraPosition", vec3(cameraPosition));
            programObject->setUniform("distanceScaleFactor", distanceScaleFactor);
            programObject->setUniform("chunkLevel", chunk.tileIndex().level);
        }
        
        // Calculate other uniform variables needed for rendering
        Geodetic2 swCorner = chunk.surfacePatch().getCorner(Quad::SOUTH_WEST);
        auto patchSize = chunk.surfacePatch().size();
        
        dmat4 modelTransform = chunk.owner().modelTransform();
        dmat4 viewTransform = data.camera.combinedViewMatrix();
        mat4 modelViewTransform = mat4(viewTransform * modelTransform);
        mat4 modelViewProjectionTransform = data.camera.projectionMatrix() * modelViewTransform;

        // Upload the uniform variables
        programObject->setUniform("modelViewProjectionTransform", modelViewProjectionTransform);
        programObject->setUniform("minLatLon", vec2(swCorner.toLonLatVec2()));
        programObject->setUniform("lonLatScalingFactor", vec2(patchSize.toLonLatVec2()));
        programObject->setUniform("radiiSquared", vec3(ellipsoid.radiiSquared()));

        if (_tileProviderManager->layerGroup(
                LayeredTextures::NightTextures).activeLayers().size() > 0 ||
            _tileProviderManager->layerGroup(
                LayeredTextures::WaterMasks).activeLayers().size() > 0 ||
            chunk.owner().generalProperties().atmosphereEnabled ||
            chunk.owner().generalProperties().performShading) {
            glm::vec3 directionToSunWorldSpace =
                glm::normalize(-data.modelTransform.translation);
            glm::vec3 directionToSunCameraSpace =
                (viewTransform * glm::dvec4(directionToSunWorldSpace, 0));
            data.modelTransform.translation;
            programObject->setUniform("modelViewTransform", modelViewTransform);
            programObject->setUniform("lightDirectionCameraSpace", -directionToSunCameraSpace);
        }

        // OpenGL rendering settings
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // render
        _grid->geometry().drawUsingActiveProgram();

        // disable shader
        programObject->deactivate();
    }



    void ChunkRenderer::renderChunkLocally(const Chunk& chunk, const RenderData& data) {
        
        ProgramObject* programObject = getActivatedProgramWithTileData(
            _localRenderingShaderProvider.get(),
            _localProgramUniformHandler,
            chunk);
        if (programObject == nullptr) {
            return;
        }

        using namespace glm;

        const Ellipsoid& ellipsoid = chunk.owner().ellipsoid();


        bool performAnyBlending = false;
        for (int i = 0; i < LayeredTextures::NUM_TEXTURE_CATEGORIES; ++i) {
            LayeredTextures::TextureCategory category = (LayeredTextures::TextureCategory)i;
            if (_tileProviderManager->layerGroup(i).levelBlendingEnabled && _tileProviderManager->layerGroup(category).activeLayers().size() > 0) {
                performAnyBlending = true;
                break;
            }
        }
        if (performAnyBlending) {
            float distanceScaleFactor = chunk.owner().generalProperties().lodScaleFactor * chunk.owner().ellipsoid().minimumRadius();
            programObject->setUniform("distanceScaleFactor", distanceScaleFactor);
            programObject->setUniform("chunkLevel", chunk.tileIndex().level);
        }

        // Calculate other uniform variables needed for rendering
        dmat4 modelTransform = chunk.owner().modelTransform();
        dmat4 viewTransform = data.camera.combinedViewMatrix();
        dmat4 modelViewTransform = viewTransform * modelTransform;

        std::vector<std::string> cornerNames = { "p01", "p11", "p00", "p10" };
        std::vector<Vec3> cornersCameraSpace(4);
        for (int i = 0; i < 4; ++i) {
            Quad q = (Quad)i;
            Geodetic2 corner = chunk.surfacePatch().getCorner(q);
            Vec3 cornerModelSpace = ellipsoid.cartesianSurfacePosition(corner);
            Vec3 cornerCameraSpace = Vec3(dmat4(modelViewTransform) * glm::dvec4(cornerModelSpace, 1));
            cornersCameraSpace[i] = cornerCameraSpace;
            programObject->setUniform(cornerNames[i], vec3(cornerCameraSpace));
        }

        vec3 patchNormalCameraSpace = normalize(
            cross(cornersCameraSpace[Quad::SOUTH_EAST] - cornersCameraSpace[Quad::SOUTH_WEST],
                cornersCameraSpace[Quad::NORTH_EAST] - cornersCameraSpace[Quad::SOUTH_WEST]));

        programObject->setUniform("patchNormalCameraSpace", patchNormalCameraSpace);
        programObject->setUniform("projectionTransform", data.camera.projectionMatrix());

        if (_tileProviderManager->layerGroup(
                LayeredTextures::NightTextures).activeLayers().size() > 0 ||
            _tileProviderManager->layerGroup(
                LayeredTextures::WaterMasks).activeLayers().size() > 0 ||
            chunk.owner().generalProperties().atmosphereEnabled ||
            chunk.owner().generalProperties().performShading) {
            glm::vec3 directionToSunWorldSpace =
                glm::normalize(-data.modelTransform.translation);
            glm::vec3 directionToSunCameraSpace =
                (viewTransform * glm::dvec4(directionToSunWorldSpace, 0));
            data.modelTransform.translation;
            programObject->setUniform("lightDirectionCameraSpace", -directionToSunCameraSpace);
        }


        // OpenGL rendering settings
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // render
        _grid->geometry().drawUsingActiveProgram();

        // disable shader
        programObject->deactivate();
    }
} // namespace globebrowsing
} // namespace openspace
