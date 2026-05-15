#pragma once
enum class PipelineType{
	Sprite, // スプライト用
	Object3D, // 3Dオブジェクト用
	Object3D_CullNone, // 3Dオブジェクト用（カリングなし）
	InstancedObject3D, // インスタンシング専用
	Particle, // パーティクル用
	PostEffect, // ポストエフェクト用
	SkinningObject3D, // スキニングアニメーション用
	Skybox, // スカイボックス用
	Object3D_Additive, // 3Dオブジェクト用（加算合成）
};