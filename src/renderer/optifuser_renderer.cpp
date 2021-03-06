#include "optifuser_renderer.h"
#include <objectLoader.h>
#include <spdlog/spdlog.h>
#define IMGUI_IMPL_OPENGL_LOADER_GLEW
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace sapien {
namespace Renderer {

static std::vector<std::string> saveNames = {};

enum RenderMode {
  LIGHTING,
  ALBEDO,
  NORMAL,
  DEPTH,
  SEGMENTATION,
  CUSTOM
#ifdef _USE_OPTIX
  ,
  PATHTRACER
#endif
};

constexpr int WINDOW_WIDTH = 1200, WINDOW_HEIGHT = 800;

//======== Begin Rigidbody ========//

OptifuserRigidbody::OptifuserRigidbody(OptifuserScene *scene,
                                       std::vector<Optifuser::Object *> const &objects)
    : mParentScene(scene), mObjects(objects) {}

void OptifuserRigidbody::setUniqueId(uint32_t uniqueId) {
  mUniqueId = uniqueId;
  for (auto obj : mObjects) {
    obj->setObjId(uniqueId);
  }
}

uint32_t OptifuserRigidbody::getUniqueId() const { return mUniqueId; }

void OptifuserRigidbody::setSegmentationId(uint32_t segmentationId) {
  mSegmentationId = segmentationId;
  for (auto obj : mObjects) {
    obj->setSegmentId(segmentationId);
  }
}

uint32_t OptifuserRigidbody::getSegmentationId() const { return mSegmentationId; }

void OptifuserRigidbody::setSegmentationCustomData(const std::vector<float> &customData) {
  for (auto obj : mObjects) {
    obj->setUserData(customData);
  }
}

void OptifuserRigidbody::setInitialPose(const physx::PxTransform &transform) {
  mInitialPose = transform;
  update({{0, 0, 0}, physx::PxIdentity});
}

void OptifuserRigidbody::update(const physx::PxTransform &transform) {
  auto pose = transform * mInitialPose;
  for (auto obj : mObjects) {
    obj->position = {pose.p.x, pose.p.y, pose.p.z};
    obj->setRotation({pose.q.w, pose.q.x, pose.q.y, pose.q.z});
  }
}

void OptifuserRigidbody::destroy() { mParentScene->removeRigidbody(this); }
void OptifuserRigidbody::destroyVisualObjects() {
  for (auto obj : mObjects) {
    mParentScene->getScene()->removeObject(obj);
  }
}

void OptifuserRigidbody::setVisible(bool visible) {
  for (auto obj : mObjects) {
    obj->visible = visible;
  }
}

void OptifuserRigidbody::setRenderMode(uint32_t mode) {
  // TODO: implement mode
}

//======== End Rigidbody ========//

//======== Begin Scene ========//
OptifuserScene::OptifuserScene(OptifuserRenderer *renderer, std::string const &name)
    : mParentRenderer(renderer), mScene(std::make_unique<Optifuser::Scene>()), mName(name) {}

Optifuser::Scene *OptifuserScene::getScene() { return mScene.get(); }

IPxrRigidbody *OptifuserScene::addRigidbody(const std::string &meshFile,
                                            const physx::PxVec3 &scale) {
  auto objects = Optifuser::LoadObj(meshFile);
  std::vector<Optifuser::Object *> objs;
  if (objects.empty()) {
    spdlog::error("Failed to load damaged file: {}", meshFile);
    return nullptr;
  }
  for (auto &obj : objects) {
    obj->scale = {scale.x, scale.y, scale.z};
    objs.push_back(obj.get());
    mScene->addObject(std::move(obj));
  }

  mBodies.push_back(std::make_unique<OptifuserRigidbody>(this, objs));

  return mBodies.back().get();
}

IPxrRigidbody *OptifuserScene::addRigidbody(std::vector<physx::PxVec3> const &points,
                                            std::vector<physx::PxVec3> const &normals,
                                            std::vector<uint32_t> const &indices,
                                            const physx::PxVec3 &scale,
                                            const physx::PxVec3 &color) {
  std::vector<Optifuser::Vertex> vertices;
  for (uint32_t i = 0; i < points.size(); ++i) {
    vertices.push_back(
        {{points[i].x, points[i].y, points[i].z}, {normals[i].x, normals[i].y, normals[i].z}});
  }

  auto obj = Optifuser::NewObject<Optifuser::Object>(
      std::make_shared<Optifuser::TriangleMesh>(vertices, indices, false));
  obj->material.kd.r = color.x;
  obj->material.kd.g = color.y;
  obj->material.kd.b = color.z;

  obj->scale = {scale.x, scale.y, scale.z};

  mBodies.push_back(std::make_unique<OptifuserRigidbody>(this, std::vector{obj.get()}));

  mScene->addObject(std::move(obj));

  return mBodies.back().get();
}

IPxrRigidbody *OptifuserScene::addRigidbody(physx::PxGeometryType::Enum type,
                                            const physx::PxVec3 &scale,
                                            const physx::PxVec3 &color) {
  std::unique_ptr<Optifuser::Object> obj;
  switch (type) {
  case physx::PxGeometryType::eBOX: {
    obj = Optifuser::NewFlatCube();
    obj->scale = {scale.x, scale.y, scale.z};
    break;
  }
  case physx::PxGeometryType::eSPHERE: {
    obj = Optifuser::NewSphere();
    obj->scale = {scale.x, scale.y, scale.z};
    break;
  }
  case physx::PxGeometryType::ePLANE: {
    obj = Optifuser::NewYZPlane();
    obj->scale = {scale.x, scale.y, scale.z};
    break;
  }
  case physx::PxGeometryType::eCAPSULE: {
    obj = Optifuser::NewCapsule(scale.x, scale.y);
    obj->scale = {1, 1, 1};
    break;
  }
  default:
    spdlog::error("Failed to add Rididbody: unimplemented shape");
    return nullptr;
  }

  obj->material.kd = {color.x, color.y, color.z, 1};

  mBodies.push_back(std::make_unique<OptifuserRigidbody>(this, std::vector{obj.get()}));
  mScene->addObject(std::move(obj));

  return mBodies.back().get();
}

void OptifuserScene::removeRigidbody(IPxrRigidbody *body) {
  auto it = mBodies.begin();
  for (; it != mBodies.end(); ++it) {
    if (it->get() == body) {
      it->get()->destroyVisualObjects();
      mBodies.erase(it);
      return;
    }
  }
}

ICamera *OptifuserScene::addCamera(std::string const &name, uint32_t width, uint32_t height,
                                   float fovx, float fovy, float near, float far,
                                   std::string const &shaderDir) {
  std::string d;
  if (shaderDir.length()) {
    d = shaderDir;
  } else {
    d = mParentRenderer->mGlslDir;
  }

  spdlog::warn("Note: current camera implementation does not support non-square pixels, and fovy "
               "will take precedence.");
  auto cam = std::make_unique<OptifuserCamera>(name, width, height, fovy, this, d);
  cam->mCameraSpec->near = near;
  cam->mCameraSpec->far = far;
  mCameras.push_back(std::move(cam));
  return mCameras.back().get();
}

void OptifuserScene::removeCamera(ICamera *camera) {
  mCameras.erase(std::remove_if(mCameras.begin(), mCameras.end(),
                                [camera](auto &c) { return camera == c.get(); }),
                 mCameras.end());
}

std::vector<ICamera *> OptifuserScene::getCameras() {
  std::vector<ICamera *> cams;
  for (auto &cam : mCameras) {
    cams.push_back(cam.get());
  }
  return cams;
}

void OptifuserScene::destroy() { mParentRenderer->removeScene(this); }

void OptifuserScene::setAmbientLight(std::array<float, 3> const &color) {
  mScene->setAmbientLight({color[0], color[1], color[2]});
}

void OptifuserScene::setShadowLight(std::array<float, 3> const &direction,
                                    std::array<float, 3> const &color) {
  mScene->setShadowLight(
      {{direction[0], direction[1], direction[2]}, {color[0], color[1], color[2]}});
}

void OptifuserScene::addPointLight(std::array<float, 3> const &position,
                                   std::array<float, 3> const &color) {
  mScene->addPointLight({{position[0], position[1], position[2]}, {color[0], color[1], color[2]}});
}

void OptifuserScene::addDirectionalLight(std::array<float, 3> const &direction,
                                         std::array<float, 3> const &color) {
  mScene->addDirectionalLight(
      {{direction[0], direction[1], direction[2]}, {color[0], color[1], color[2]}});
}

//======== End Scene ========//

//======== Begin Renderer ========//

OptifuserRenderer::OptifuserRenderer(const std::string &glslDir, const std::string &glslVersion) {
  if (glslDir.length()) {
    mGlslDir = glslDir;
  } else {
    mGlslDir = gDefaultGlslDir;
  }

  mContext = &Optifuser::GLFWRenderContext::Get(WINDOW_WIDTH, WINDOW_HEIGHT);
  mContext->initGui(glslVersion.length() ? glslVersion : gDefaultGlslVersion);

  mContext->renderer.setShadowShader(mGlslDir + "/shadow.vsh", mGlslDir + "/shadow.fsh");
  mContext->renderer.setGBufferShader(mGlslDir + "/gbuffer.vsh",
                                      mGlslDir + "/gbuffer_segmentation.fsh");
  mContext->renderer.setDeferredShader(mGlslDir + "/deferred.vsh", mGlslDir + "/deferred.fsh");
  mContext->renderer.setAxisShader(mGlslDir + "/axes.vsh", mGlslDir + "/axes.fsh");
  mContext->renderer.enablePicking();
  mContext->renderer.enableAxisPass();
}

IPxrScene *OptifuserRenderer::createScene(std::string const &name) {
  mScenes.push_back(std::make_unique<OptifuserScene>(this, name));
  return mScenes.back().get();
}

void OptifuserRenderer::removeScene(IPxrScene *scene) {
  mScenes.erase(std::remove_if(mScenes.begin(), mScenes.end(),
                               [scene](auto &s) { return scene == s.get(); }),
                mScenes.end());
}

std::string OptifuserRenderer::gDefaultGlslDir = "glsl_shader/130";
std::string OptifuserRenderer::gDefaultGlslVersion = "130";
void OptifuserRenderer::setDefaultShaderConfig(std::string const &glslDir,
                                               std::string const &glslVersion) {
  gDefaultGlslDir = glslDir;
  gDefaultGlslVersion = glslVersion;
}

#ifdef _USE_OPTIX
std::string OptifuserRenderer::gPtxDir = "ptx";
void OptifuserRenderer::setOptixConfig(std::string const &ptxDir) {
  gPtxDir = ptxDir;
}
#endif

void OptifuserRenderer::enableGlobalAxes(bool enable) {
  mContext->renderer.enableGlobalAxes(enable);
}

//======== End Renderer ========//

} // namespace Renderer
} // namespace sapien
