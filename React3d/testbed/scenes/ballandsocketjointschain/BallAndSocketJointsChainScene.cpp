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
#include "BallAndSocketJointsChainScene.h"
#include <cmath>

// Namespaces
using namespace openglframework;
using namespace ballandsocketjointschainscene;

// Constructor
BallAndSocketJointsChainScene::BallAndSocketJointsChainScene(const std::string& name, EngineSettings& settings, reactphysics3d::PhysicsCommon& physicsCommon)
      : SceneDemo(name, settings, physicsCommon, true, SCENE_RADIUS) {

    std::string meshFolderPath("meshes/");

    // Compute the radius and the center of the scene
    openglframework::Vector3 center(0, 0, 0);

    // Set the center of the scene
    setScenePosition(center, SCENE_RADIUS);
    setInitZoom(1.3);
    resetCameraToViewAll();

    mWorldSettings.worldName = name;
}

// Destructor
BallAndSocketJointsChainScene::~BallAndSocketJointsChainScene() {

    destroyPhysicsWorld();
}

// Create the physics world
void BallAndSocketJointsChainScene::createPhysicsWorld() {

    // Gravity vector in the physics world
    mWorldSettings.gravity = rp3d::Vector3(mEngineSettings.gravity.x, mEngineSettings.gravity.y, mEngineSettings.gravity.z);

    // Create the physics world for the physics simulation
    mPhysicsWorld = mPhysicsCommon.createPhysicsWorld(mWorldSettings);
    mPhysicsWorld->setEventListener(this);

    // Create all the spheres of the scene
    for (int i=0; i<NB_SPHERES; i++) {

        // Create a sphere and a corresponding rigid in the physics world
        mSpheres[i] = new Sphere(true, SPHERE_RADIUS, mPhysicsCommon, mPhysicsWorld, mMeshFolderPath);

        // Set the sphere color
        mSpheres[i]->setColor(mObjectColorDemo);
        mSpheres[i]->setSleepingColor(mSleepingColorDemo);

        // Change the material properties of the rigid body
        rp3d::Material& material = mSpheres[i]->getCollider()->getMaterial();
        material.setBounciness(rp3d::decimal(0.0));

        if (i == 0) {
            mSpheres[i]->getRigidBody()->setType(rp3d::BodyType::STATIC);
        }

        // Add the sphere the list of sphere in the scene
        mPhysicsObjects.push_back(mSpheres[i]);
    }

    // Set the position of the spheres before the joints creation
    initBodiesPositions();

    // Create the Ball-and-Socket joints
    createJoints();
}

// Initialize the bodies positions
void BallAndSocketJointsChainScene::initBodiesPositions() {

    const float space = 0.5f;

    const rp3d::Quaternion initOrientation = rp3d::Quaternion::identity();

    for (int i=0; i<NB_SPHERES; i++) {

        // Initial position and orientation of the rigid body
        rp3d::Vector3 initPosition(i * (2 * SPHERE_RADIUS + space), 20, 0);
        rp3d::Transform transform(initPosition, initOrientation);

        // Create a box and a corresponding rigid in the physics world
        mSpheres[i]->setTransform(transform);
    }
}

// Destroy the physics world
void BallAndSocketJointsChainScene::destroyPhysicsWorld() {

    if (mPhysicsWorld != nullptr) {

        // Destroy all the physics objects of the scene
        for (std::vector<PhysicsObject*>::iterator it = mPhysicsObjects.begin(); it != mPhysicsObjects.end(); ++it) {

            // Destroy the object
            delete (*it);
        }

        mBallAndSocketJoints.clear();
        mPhysicsObjects.clear();

        mPhysicsCommon.destroyPhysicsWorld(mPhysicsWorld);
        mPhysicsWorld = nullptr;
    }
}

// Create the joints
void BallAndSocketJointsChainScene::createJoints() {

    for (int i=0; i < NB_SPHERES-1; i++) {

        // Create the joint info object
        rp3d::RigidBody* body1 = mSpheres[i]->getRigidBody();
        rp3d::RigidBody* body2 = mSpheres[i+1]->getRigidBody();
        rp3d::Vector3 body1Position = body1->getTransform().getPosition();
        const rp3d::Vector3 anchorPointWorldSpace = body1Position;
        rp3d::BallAndSocketJointInfo jointInfo(body1, body2, anchorPointWorldSpace);
        jointInfo.isCollisionEnabled = false;
        rp3d::BallAndSocketJoint* joint = dynamic_cast<rp3d::BallAndSocketJoint*>( mPhysicsWorld->createJoint(jointInfo));
        mBallAndSocketJoints.push_back(joint);
    }
}

// Reset the scene
void BallAndSocketJointsChainScene::reset() {

    SceneDemo::reset();

    destroyPhysicsWorld();
    createPhysicsWorld();
    initBodiesPositions();
}
