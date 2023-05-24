/********************************************************************************
* ReactPhysics3D physics library, http://www.reactphysics3d.com                 *
* Copyright (c) 2010-2016 Daniel Chappuis                                       *
*********************************************************************************
*                                                                               *
* This software is provided 'as-is', without any express or implied warranty.   *
* In no event will the authors be held liable for any damages arising from the  *
* use of this software.                                                         *
*                                                                               *
* Permission is granted to anyone to use this software for any purpose,         *
* including commercial applications, and to alter it and redistribute it        *
* freely, subject to the following restrictions:                                *
*                                                                               *
* 1. The origin of this software must not be misrepresented; you must not claim *
*    that you wrote the original software. If you use this software in a        *
*    product, an acknowledgment in the product documentation would be           *
*    appreciated but is not required.                                           *
*                                                                               *
* 2. Altered source versions must be plainly marked as such, and must not be    *
*    misrepresented as being the original software.                             *
*                                                                               *
* 3. This notice may not be removed or altered from any source distribution.    *
*                                                                               *
********************************************************************************/

// Libraries
#include "ConcaveMeshScene.h"

// Namespaces
using namespace openglframework;
using namespace trianglemeshscene;

// Constructor
ConcaveMeshScene::ConcaveMeshScene(const std::string& name, EngineSettings& settings, reactphysics3d::PhysicsCommon& physicsCommon)
      : SceneDemo(name, settings, physicsCommon, true, SCENE_RADIUS) {

    std::string meshFolderPath("meshes/");

    // Compute the radius and the center of the scene
    openglframework::Vector3 center(0, 15, 0);

    // Set the center of the scene
    setScenePosition(center, SCENE_RADIUS);
    setInitZoom(1.5);
    resetCameraToViewAll();

    mWorldSettings.worldName = name;
}

// Destructor
ConcaveMeshScene::~ConcaveMeshScene() {

    destroyPhysicsWorld();
}

// Create the physics world
void ConcaveMeshScene::createPhysicsWorld() {

    // Gravity vector in the physics world
    mWorldSettings.gravity = rp3d::Vector3(mEngineSettings.gravity.x, mEngineSettings.gravity.y, mEngineSettings.gravity.z);

    // Create the physics world for the physics simulation
    rp3d::PhysicsWorld* physicsWorld = mPhysicsCommon.createPhysicsWorld(mWorldSettings);
    physicsWorld->setEventListener(this);
    mPhysicsWorld = physicsWorld;

    // ---------- Create the boxes ----------- //
    for (int i = 0; i<NB_COMPOUND_SHAPES; i++) {

        // Create a convex mesh and a corresponding rigid in the physics world
        Dumbbell* dumbbell = new Dumbbell(true, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath);

        // Set the box color
        dumbbell->setColor(mObjectColorDemo);
        dumbbell->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& capsuleMaterial = dumbbell->getCapsuleCollider()->getMaterial();
        capsuleMaterial.setBounciness(rp3d::decimal(0.2));
        rp3d::Material& sphere1Material = dumbbell->getSphere1Collider()->getMaterial();
        sphere1Material.setBounciness(rp3d::decimal(0.2));
        rp3d::Material& sphere2Material = dumbbell->getSphere2Collider()->getMaterial();
        sphere2Material.setBounciness(rp3d::decimal(0.2));

        // Add the mesh the list of dumbbells in the scene
        mDumbbells.push_back(dumbbell);
        mPhysicsObjects.push_back(dumbbell);
    }

    // Create all the boxes of the scene
    for (int i = 0; i<NB_BOXES; i++) {

        // Create a sphere and a corresponding rigid in the physics world
        Box* box = new Box(true, BOX_SIZE, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath);

        // Set the box color
        box->setColor(mObjectColorDemo);
        box->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& material = box->getCollider()->getMaterial();
        material.setBounciness(rp3d::decimal(0.2));

        // Add the sphere the list of sphere in the scene
        mBoxes.push_back(box);
        mPhysicsObjects.push_back(box);
    }

    // Create all the spheres of the scene
    for (int i = 0; i<NB_SPHERES; i++) {

        // Create a sphere and a corresponding rigid in the physics world
        Sphere* sphere = new Sphere(true, SPHERE_RADIUS, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath);

        // Set the box color
        sphere->setColor(mObjectColorDemo);
        sphere->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& material = sphere->getCollider()->getMaterial();
        material.setBounciness(rp3d::decimal(0.2));

        // Add the sphere the list of sphere in the scene
        mSpheres.push_back(sphere);
        mPhysicsObjects.push_back(sphere);
    }

    // Create all the capsules of the scene
    for (int i = 0; i<NB_CAPSULES; i++) {

        // Create a cylinder and a corresponding rigid in the physics world
        Capsule* capsule = new Capsule(true, CAPSULE_RADIUS, CAPSULE_HEIGHT,
                                       mPhysicsCommon, mPhysicsWorld, mMeshFolderPath);

        // Set the box color
        capsule->setColor(mObjectColorDemo);
        capsule->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& material = capsule->getCollider()->getMaterial();
        material.setBounciness(rp3d::decimal(0.2));

        // Add the cylinder the list of sphere in the scene
        mCapsules.push_back(capsule);
        mPhysicsObjects.push_back(capsule);
    }

    // Create all the convex meshes of the scene
    for (int i = 0; i<NB_MESHES; i++) {

        // Create a convex mesh and a corresponding rigid in the physics world
        ConvexMesh* mesh = new ConvexMesh(true, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath + "convexmesh.obj");

        // Set the box color
        mesh->setColor(mObjectColorDemo);
        mesh->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& material = mesh->getCollider()->getMaterial();
        material.setBounciness(rp3d::decimal(0.2));

        // Add the mesh the list of sphere in the scene
        mConvexMeshes.push_back(mesh);
        mPhysicsObjects.push_back(mesh);
    }

    // ---------- Create the triangular mesh ---------- //

    // Create a convex mesh and a corresponding rigid in the physics world
    mConcaveMesh = new ConcaveMesh(true, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath + "castle.obj", rp3d::Vector3(0.5, 0.5, 0.5));

    // Set the mesh as beeing static
    mConcaveMesh->getRigidBody()->setType(rp3d::BodyType::STATIC);

    // Set the box color
    mConcaveMesh->setColor(mFloorColorDemo);
    mConcaveMesh->setSleepingColor(mFloorColorDemo);

    mPhysicsObjects.push_back(mConcaveMesh);

    // Change the material properties of the rigid body
    rp3d::Material& material = mConcaveMesh->getCollider()->getMaterial();
    material.setBounciness(rp3d::decimal(0.2));
    material.setFrictionCoefficient(rp3d::decimal(0.1));
}

// Initialize the bodies positions
void ConcaveMeshScene::initBodiesPositions() {

    const float radius = 15.0f;

    for (uint i = 0; i<NB_COMPOUND_SHAPES; i++) {

        // Position
        float angle = i * 30.0f;
        rp3d::Vector3 position(radius * std::cos(angle),
            125 + i * (DUMBBELL_HEIGHT + 0.3f),
            radius * std::sin(angle));

        mDumbbells[i]->setTransform(rp3d::Transform(position, rp3d::Quaternion::identity()));
    }

    // Create all the boxes of the scene
    for (uint i = 0; i<NB_BOXES; i++) {

        // Position
        float angle = i * 30.0f;
        rp3d::Vector3 position(radius * std::cos(angle),
            85 + i * (BOX_SIZE.y + 0.8f),
            radius * std::sin(angle));

        mBoxes[i]->setTransform(rp3d::Transform(position, rp3d::Quaternion::identity()));
    }

    // Create all the spheres of the scene
    for (uint i = 0; i<NB_SPHERES; i++) {

        // Position
        float angle = i * 35.0f;
        rp3d::Vector3 position(radius * std::cos(angle),
            75 + i * (SPHERE_RADIUS + 0.8f),
            radius * std::sin(angle));

        mSpheres[i]->setTransform(rp3d::Transform(position, rp3d::Quaternion::identity()));
    }

    // Create all the capsules of the scene
    for (uint i = 0; i<NB_CAPSULES; i++) {

        // Position
        float angle = i * 45.0f;
        rp3d::Vector3 position(radius * std::cos(angle),
            40 + i * (CAPSULE_HEIGHT + 0.3f),
            radius * std::sin(angle));

        mCapsules[i]->setTransform(rp3d::Transform(position, rp3d::Quaternion::identity()));
    }

    // Create all the convex meshes of the scene
    for (uint i = 0; i<NB_MESHES; i++) {

        // Position
        float angle = i * 30.0f;
        rp3d::Vector3 position(radius * std::cos(angle),
            30 + i * (CAPSULE_HEIGHT + 0.3f),
            radius * std::sin(angle));

        mConvexMeshes[i]->setTransform(rp3d::Transform(position, rp3d::Quaternion::identity()));
    }

    // ---------- Create the triangular mesh ---------- //


    mConcaveMesh->setTransform(rp3d::Transform(rp3d::Vector3(15, 0, -5), rp3d::Quaternion::identity()));
}

// Destroy the physics world
void ConcaveMeshScene::destroyPhysicsWorld() {

    if (mPhysicsWorld != nullptr) {

        // Destroy all the physics objects of the scene
        for (std::vector<PhysicsObject*>::iterator it = mPhysicsObjects.begin(); it != mPhysicsObjects.end(); ++it) {

            // Destroy the object
            delete (*it);
        }
        mBoxes.clear();
        mSpheres.clear();
        mCapsules.clear();
        mConvexMeshes.clear();
        mDumbbells.clear();

        mPhysicsObjects.clear();

        mPhysicsCommon.destroyPhysicsWorld(mPhysicsWorld);
        mPhysicsWorld = nullptr;
    }
}
// Reset the scene
void ConcaveMeshScene::reset() {

    SceneDemo::reset();

    destroyPhysicsWorld();
    createPhysicsWorld();
    initBodiesPositions();
}
