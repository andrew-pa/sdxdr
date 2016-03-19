#pragma once
#pragma once
#include "dxut\dxut.h"
#include "dxut\gamepad.hpp"
#include "meshloader.h"
#include "renderer.h"
#include "model.hpp"

#include <btBulletDynamicsCommon.h>

inline btVector3 x3_b3(XMFLOAT3 x) {
	return btVector3(x.x, x.y, x.z);
}
inline btVector3 a3_b3(const aiVector3D& x) {
	return btVector3(x.x, x.y, x.z);
}
inline XMFLOAT3 b3_x3(const btVector3& b3) {
	return XMFLOAT3(b3.x(), b3.y(), b3.z());
}

inline aiVector3D trf(const aiVector3D& v, const XMMATRIX& m) {
	XMVECTOR V = XMVectorSet(v.x, v.y, v.z, 1.f);
	V = XMVector3TransformCoord(V, m);
	return aiVector3D(XMVectorGetX(V), XMVectorGetY(V), XMVectorGetZ(V));
}

struct game_app : public DXWindow, public DXDevice {

	game_app()
		: DXWindow(2560, 1920, L"sdxdr"), DXDevice(), dfr(nullptr), gmp(0) {}

	shared_ptr<renderer> dfr;

	StepTimer tim;

	SimpleCamera cam;

	model car_body;
	model car_tires[4];
	model track;

	btDiscreteDynamicsWorld* world;
	btRigidBody* track_body;
	btRigidBody* create_body(btCollisionShape* shape, float mass, const btTransform& tf) {
		auto ms = new btDefaultMotionState(tf);
		btVector3 locint(0.f, 0.f, 0.f);
		if (mass > 0.f) shape->calculateLocalInertia(mass, locint);
		return new btRigidBody(btRigidBody::btRigidBodyConstructionInfo(mass, ms, shape, locint));
	}

	btRigidBody* car_chassis;

	btRaycastVehicle::btVehicleTuning tuning;
	btVehicleRaycaster* vhcRc;
	btRaycastVehicle* vehicle;

	bool follow_camera;

	float engine_force, brake_force, steering_direction;

	dxut::gamepad gmp;

	void OnInit() override {
		init_d3d(this);

		follow_camera = true;

#pragma region Init Physics
		auto colfig = new btDefaultCollisionConfiguration;
		auto disp = new btCollisionDispatcher(colfig);
		auto opc = new btDbvtBroadphase;
		auto slv = new btSequentialImpulseConstraintSolver;

		world = new btDiscreteDynamicsWorld(disp, opc, slv, colfig);
		world->setGravity(btVector3(0.f, -9.81f, 0.f));
		btTransform tf;
#pragma endregion


		vector<shared_ptr<render_object>> ros;

		commandList = create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT);

		//vector<vertex> v; vector<uint32_t> i;
		//generate_cube_mesh(v, i, XMFLOAT3(3.f, 3.f, 3.f));
		//auto cube_mesh = make_shared<mesh>(this, commandList, v, i);

		XMFLOAT4X4 w;// XMStoreFloat4x4(&w, XMMatrixScaling(40.f, 1.f, 40.f));
		//ros.push_back(make_shared<render_object>(cube_mesh, material(XMFLOAT4(0.2f, 0.2f, 0.2f, 1.f)), w));

		Assimp::Importer imp;
#pragma region Load Car
		auto scn = imp.ReadFile(R"(C:\Users\andre\Downloads\3DModels\porche_speedster_55.fbx)", //ws2s(GetAssetFullPath(L"thing.fbx")),
			aiProcessPreset_TargetRealtime_Quality);

		auto carmats = load_materials(scn);
		auto car_all_w = XMMatrixTranslation(9.f, 20.f, 0.f);
		XMStoreFloat4x4(&w, XMMatrixRotationX(-XM_PIDIV2) *  car_all_w);
		car_body = model(this, commandList, ros, scn, "body", carmats, w);

		car_tires[0] = model(this, commandList, ros, scn, "tire", carmats, w);
		car_tires[1] = car_tires[0].make_instance(ros, w);
		car_tires[2] = car_tires[0].make_instance(ros, w);
		car_tires[3] = car_tires[0].make_instance(ros, w);

		//create car
		auto chassis_extent = btVector3(2.66935706, 1.70578206, 6.71773052); //calc'd by kdx12
		btCompoundShape* chassisShp = new btCompoundShape();
		btTransform lt;
		lt.setIdentity(); lt.setOrigin(btVector3(0, 0, 0));
		chassisShp->addChildShape(lt, new btBoxShape(chassis_extent));
		tf.setIdentity();
		tf.setOrigin(btVector3(10.f, 5.f, 0.f));
		tf.setRotation(btQuaternion(-XM_PIDIV2, 0.f, 0.f));
		car_chassis = create_body(chassisShp, 2000.0f, tf);
		world->addRigidBody(car_chassis);
		tuning = btRaycastVehicle::btVehicleTuning{};

		vhcRc = new btDefaultVehicleRaycaster(world);
		vehicle = new btRaycastVehicle(tuning, car_chassis, vhcRc);

		car_chassis->setActivationState(DISABLE_DEACTIVATION);

		world->addVehicle(vehicle);

		vehicle->setCoordinateSystem(0, 1, 2);
		
		const float srl = .6f;
		const float whlr = 1.14171398f+.1f; //same as above
		const float why = -whlr*.8f;
		const float tire_h = -1.f;
		const float ft_tire_z = 3.5f;
		const float bk_tire_z = -4.9f;
		const float car_w = 2.5f;
		vehicle->addWheel(btVector3(car_w, tire_h, -ft_tire_z), btVector3(0.f, -1.f, 0.f), btVector3(1.f, 0.f, 0.f),
			srl, whlr, tuning, true);
		vehicle->addWheel(btVector3(-car_w+.3f, tire_h, -ft_tire_z), btVector3(0.f, -1.f, 0.f), btVector3(1.f, 0.f, 0.f),
			srl, whlr, tuning, true);

		vehicle->addWheel(btVector3(car_w, tire_h, -bk_tire_z), btVector3(0.f, -1.f, 0.f), btVector3(1.f, 0.f, 0.f),
			srl, whlr, tuning, false);
		vehicle->addWheel(btVector3(-car_w+.3f, tire_h, -bk_tire_z), btVector3(0.f, -1.f, 0.f), btVector3(1.f, 0.f, 0.f),
			srl, whlr, tuning, false);


		for (int i = 0; i < vehicle->getNumWheels(); ++i) {
			btWheelInfo& wh = vehicle->getWheelInfo(i);
			wh.m_suspensionStiffness = 15.f;
			wh.m_wheelsDampingRelaxation = 100.0f;
			wh.m_wheelsDampingCompression = 100.0f;
			wh.m_frictionSlip = 2000;
			wh.m_rollInfluence = 0.1f;
			vehicle->updateWheelTransform(i, true);
		}
		

		engine_force =
			brake_force =
			steering_direction = 0.f;
#pragma endregion
#pragma region Load Track
		auto trkscn = imp.ReadFile(R"(C:\Users\andre\Downloads\3DModels\Race Track\track0.fbx)", aiProcessPreset_TargetRealtime_Quality);
		auto m = XMMatrixRotationX(-XM_PIDIV2)*XMMatrixScaling(11.9f, 1.2f, 11.9f);
		XMStoreFloat4x4(&w, m);
		track = model(this, commandList, ros, trkscn, "", load_materials(trkscn), w);
#pragma region Build Track Shape
		auto mi = new btTriangleMesh();
		for (int i = 0; i < trkscn->mNumMeshes; ++i) {
			auto trkmsh = trkscn->mMeshes[i];
			for (int j = 0; j < trkmsh->mNumFaces; j++) {
				mi->addTriangle(a3_b3(trf(trkmsh->mVertices[trkmsh->mFaces[j].mIndices[0]], m)),
					a3_b3(trf(trkmsh->mVertices[trkmsh->mFaces[j].mIndices[1]], m)),
					a3_b3(trf(trkmsh->mVertices[trkmsh->mFaces[j].mIndices[2]], m)));
			}
		}
		auto track_shape = new btBvhTriangleMeshShape(mi, false);
		//auto track_shape = new btBoxShape(btVector3(80.f, .5f, 80.f));
		tf.setIdentity();
		//stf.setRotation(btQuaternion(-XM_PIDIV2, 0, 0));
		//track_shape->setLocalScaling(btVector3(11.9f, 1.2f, 11.9f));
		track_body = create_body(track_shape, 0.f, tf);
		world->addRigidBody(track_body);
#pragma endregion
#pragma endregion


		dfr = make_shared<renderer>(this, this, ros);
#pragma region Lights
		dfr->directional_lights.push_back(
			directional_light(XMFLOAT4(0.2f, .7f, .3f, 0.f),
				XMFLOAT4(1.f, .9f, .8f, 1.f), false, 256.f));
		dfr->directional_lights.push_back(
			directional_light(XMFLOAT4(0.2f, .7f, -.3f, 0.f),
				XMFLOAT4(.9f, 1.f, .8f, 1.f), false));
		dfr->directional_lights.push_back(
			directional_light(XMFLOAT4(-0.2f, .7f, .3f, 0.f),
				XMFLOAT4(.8f, .9f, 1.f, 1.f), false));
		dfr->directional_lights.push_back(
			directional_light(XMFLOAT4(-0.2f, -.5f, -.6f, 0.f),
				XMFLOAT4(.4f, .6f, .8f, 1.f), false));
#pragma endregion

		car_body.init(dfr);
		for (int i = 0; i < 4; ++i) car_tires[i].init(dfr);
		track.init(dfr);

		commandList->Close();

		execute_command_list();
		wait_for_gpu();
		empty_upload_pool();

		cam.Init(XMFLOAT3(6.f, 18.f, 25.f));
		cam.pitch = -XM_PIDIV4;


	}

	void OnUpdate() override {
		start_frame();
		tim.Tick(nullptr);

		gmp.update();

		if (gmp.connected) {
			engine_force = gmp.right_trigger()*8000.f * 
				(gmp.is_button_down(XINPUT_GAMEPAD_LEFT_SHOULDER) ? -1.f : 1.f);
			brake_force = gmp.left_trigger()*3000.f;
			steering_direction = -gmp.left_thumb().x*.4f;
			if (gmp.is_button_down(XINPUT_GAMEPAD_BACK)) {
				btTransform tf;
				tf.setIdentity();
				tf.setOrigin(btVector3(10.f, 5.f, 0.f));
				tf.setRotation(btQuaternion(-XM_PIDIV2, 0.f, 0.f));
				car_chassis->setWorldTransform(tf);
			}
		}

		vehicle->applyEngineForce(engine_force, 2);
		vehicle->applyEngineForce(engine_force, 3);
		vehicle->setBrake(brake_force, 2);
		vehicle->setBrake(brake_force, 3);
		vehicle->setSteeringValue(steering_direction, 0);
		vehicle->setSteeringValue(steering_direction, 1);

		world->stepSimulation(tim.GetElapsedSeconds(), 1);

		btTransform tf;
		car_chassis->getMotionState()->getWorldTransform(tf);
		auto org = tf.getOrigin();
		auto orgx3 = b3_x3(org);
		XMVECTOR car_pos = XMLoadFloat3(&orgx3);
		auto qatrt = XMVectorSet(tf.getRotation().x(), tf.getRotation().y(), tf.getRotation().z(), tf.getRotation().w());
		XMStoreFloat4x4(&car_body.world, XMMatrixRotationRollPitchYaw(-XM_PIDIV2, XM_PIDIV2, XM_PIDIV2) *
			XMMatrixRotationQuaternion(qatrt) * XMMatrixTranslation(org.x(), org.y(), org.z()));
		car_body.update();

		for (int i = 0; i < vehicle->getNumWheels(); ++i) {
			vehicle->updateWheelTransform(i, true);
			tf = vehicle->getWheelInfo(i).m_worldTransform;
			org = tf.getOrigin();
			qatrt = XMVectorSet(tf.getRotation().x(), tf.getRotation().y(), tf.getRotation().z(), tf.getRotation().w());
			XMStoreFloat4x4(&car_tires[i].world,
				XMMatrixRotationY(i%2 == 0 ? 0 : XM_PI) * XMMatrixRotationQuaternion(qatrt) * XMMatrixTranslation(org.x(), org.y(), org.z()));
			car_tires[i].update();
		}

		track.update();

		if (!follow_camera) {
			cam.Update(tim.GetElapsedSeconds());
			XMStoreFloat4x4(&dfr->cam.viewproj, cam.GetViewMatrix()*cam.GetProjectionMatrix(45.f, aspectRatio));
			dfr->cam.position = cam.GetPosition();
		}
		else {
			XMFLOAT3 fvec = b3_x3(vehicle->getForwardVector());
			XMVECTOR cam_ofs = XMVectorAdd(XMLoadFloat3(&fvec)*18.f, XMVectorSet(0.f, 7.f, 0.5f, 0.f));
			XMVECTOR cam_pos = XMVectorAdd(car_pos, cam_ofs);
			auto vp = XMMatrixLookAtRH(cam_pos, car_pos, XMVectorSet(0, 1, 0, 0)) *
				XMMatrixPerspectiveFovRH(45.f, (float)width / (float)height, 0.01f, 1000.f);
			XMStoreFloat4x4(&dfr->cam.viewproj, vp);
			XMStoreFloat3(&dfr->cam.position, cam_pos);
		}
	}

	void OnRender() override {
		wchar_t out[32];
		wsprintf(out, L"FPS: %d\n", tim.GetFramesPerSecond());
		OutputDebugStringW(out);

		dfr->render();
		execute_command_list();
		next_frame();
	}

	void OnDestroy() override {
		wait_for_gpu();
		dfr.reset();
		destroy_d3d();
	}

	bool OnEvent(MSG msg) override {
		switch (msg.message)
		{
		case WM_KEYDOWN:
			switch (msg.wParam)
			{
			case 'I':
				engine_force = 7000.f;
				break;
			case 'H':
				brake_force = 1500.f;
				break;
			case 'K':
				engine_force = -6000.f;
				break;
			case 'J':
				steering_direction = .5f;
				break;
			case 'L':
				steering_direction = -.5f;
				break;
			case 'O':
				car_chassis->applyTorque(btVector3(0.f, 10000.f, 100000.f));
				break;
			}
			cam.OnKeyDown(msg.wParam);
			break;

		case WM_KEYUP:
			switch (msg.wParam)
			{
			case 'I':
			case 'K':
				engine_force = 0.f;
				break;
			case 'H':
				brake_force = 0.f;
				break;
			case 'J':
			case 'L':
				steering_direction = 0.f;
				break;
			case VK_F3:
				follow_camera = !follow_camera;
				break;
			}
			cam.OnKeyUp(msg.wParam);
			break;
		}
		return false;
	}
};