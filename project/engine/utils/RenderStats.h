#pragma once
// =============================================================
//  RenderStats : 1フレームの描画統計（ドローコール数など）
//  - ヘッダオンリー（.vcxproj への追加不要）
//  - Framework::Draw() の先頭で BeginFrame() を呼ぶ
//  - 各描画クラス（Obj3d / SkinnedObj3d / Sprite 等）の Draw() で AddDrawCall()
//  - エディタの性能モニタで表示する
// =============================================================
#include <cstdint>

class RenderStats {
public:
	static RenderStats* GetInstance() {
		static RenderStats instance;
		return &instance;
	}

	// フレーム開始時にカウンタをリセット
	void BeginFrame() {
		drawCalls_ = 0;
		triangles_ = 0;
	}

	// 描画呼び出しを1回計上（任意で三角形数も）
	void AddDrawCall(uint32_t triangleCount = 0) {
		++drawCalls_;
		triangles_ += triangleCount;
	}

	uint32_t GetDrawCalls() const { return drawCalls_; }
	uint32_t GetTriangles() const { return triangles_; }

private:
	RenderStats() = default;
	RenderStats(const RenderStats&) = delete;
	RenderStats& operator=(const RenderStats&) = delete;

	uint32_t drawCalls_ = 0;
	uint32_t triangles_ = 0;
};
