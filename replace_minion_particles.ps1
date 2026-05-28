$path = "C:\Game\CG2\CG2_00_01\project\game\enemy\EnemyManager.cpp"
$content = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)

# CRLF -> LF に正規化してから処理
$content = $content.Replace("`r`n", "`n")

$oldBlock = @'
		// ① 足元から這い上がるような、赤黒い禍々しいオーラ
		for (int j = 0; j < 40; j++) {
			Vector3 auraPos = {
				spawnPos.x + (rand() % 21 - 10) * 0.08f,
				spawnPos.y + 0.1f + (rand() % 10) * 0.05f, // 足元から
				spawnPos.z + (rand() % 21 - 10) * 0.08f
			};
			Vector3 auraVel = {
				(rand() % 11 - 5) * 0.015f,
				0.15f + (rand() % 10) * 0.03f, // 上に向かってフワァッと噴き出す
				(rand() % 11 - 5) * 0.015f
			};

			// 赤黒い闇の魔法カラー
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 0.8f, 0.0f, 0.0f, 0.8f } : Vector4{ 0.4f, 0.0f, 0.6f, 0.8f };
			float scale = 0.8f + (rand() % 6) * 0.1f;

			// 寿命を少し長くして（0.8秒）余韻を残す
			GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.8f, scale, color);
		}

		// ② 出現時のちょっとした衝撃波（紫色の波紋）
		for (int j = 0; j < 20; j++) {
			float ringAngle = (3.141592f * 2.0f / 20.0f) * j;
			float speed = 0.25f;
			Vector3 ringVel = { std::sinf(ringAngle) * speed, 0.0f, std::cosf(ringAngle) * speed };
			GPUParticleManager::GetInstance()->Emit(spawnPos, ringVel, 0.4f, 1.2f, { 0.6f, 0.0f, 0.9f, 1.0f });
		}
'@

$newBlock = @'
		// ① 足元から這い上がるオーラ（強化版: 60個・大型スケール）
		for (int j = 0; j < 60; j++) {
			Vector3 auraPos = {
				spawnPos.x + (rand() % 21 - 10) * 0.08f,
				spawnPos.y + 0.1f + (rand() % 10) * 0.05f,
				spawnPos.z + (rand() % 21 - 10) * 0.08f
			};
			Vector3 auraVel = {
				(rand() % 11 - 5) * 0.015f,
				0.15f + (rand() % 10) * 0.03f,
				(rand() % 11 - 5) * 0.015f
			};
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 0.8f, 0.0f, 0.0f, 0.85f } : Vector4{ 0.4f, 0.0f, 0.6f, 0.85f };
			float scale = 2.0f + (rand() % 8) * 0.2f;
			GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.8f, scale, color);
		}

		// ② 出現衝撃波（強化版: 60個・大型・二重リング）
		for (int j = 0; j < 60; j++) {
			float ringAngle = (3.141592f * 2.0f / 60.0f) * j;
			float speed = (j % 2 == 0) ? 0.35f : 0.55f;
			float sc = (j % 2 == 0) ? 3.0f + (rand() % 6) * 0.3f : 4.5f + (rand() % 6) * 0.3f;
			Vector4 rc = (j % 2 == 0)
				? Vector4{ 0.9f, 0.5f, 1.0f, 0.9f }
				: Vector4{ 0.5f, 0.0f, 0.7f, 0.85f };
			Vector3 ringVel = { std::sinf(ringAngle) * speed, 0.0f, std::cosf(ringAngle) * speed };
			GPUParticleManager::GetInstance()->Emit(spawnPos, ringVel, 0.5f, sc, rc);
		}

		// ③ 召喚柱（縦の光の柱・上空へ射出）
		for (int j = 0; j < 25; j++) {
			Vector3 colPos = {
				spawnPos.x + (rand() % 7 - 3) * 0.08f,
				spawnPos.y,
				spawnPos.z + (rand() % 7 - 3) * 0.08f
			};
			Vector3 colVel = {
				(rand() % 5 - 2) * 0.02f,
				0.4f + (rand() % 8) * 0.05f,
				(rand() % 5 - 2) * 0.02f
			};
			Vector4 colColor = (rand() % 2 == 0) ? Vector4{ 0.8f, 0.1f, 0.4f, 0.9f } : Vector4{ 0.6f, 0.0f, 0.8f, 0.85f };
			float colScale = 1.5f + (rand() % 5) * 0.2f;
			GPUParticleManager::GetInstance()->Emit(colPos, colVel, 0.5f, colScale, colColor);
		}

		// ④ 着地フラッシュ（超大型・瞬間光）
		for (int j = 0; j < 4; j++) {
			Vector3 flashPos = {
				spawnPos.x + (rand() % 5 - 2) * 0.1f,
				spawnPos.y + 0.5f,
				spawnPos.z + (rand() % 5 - 2) * 0.1f
			};
			Vector3 flashVel = { 0.0f, 0.02f, 0.0f };
			float flashScale = 7.0f + (rand() % 6) * 0.5f;
			GPUParticleManager::GetInstance()->Emit(flashPos, flashVel, 0.2f, flashScale, { 1.0f, 0.8f, 1.0f, 0.85f });
		}

		// ⑤ 降り注ぐ破片（上に飛んで散る）
		for (int j = 0; j < 15; j++) {
			Vector3 debrisPos = {
				spawnPos.x + (rand() % 11 - 5) * 0.15f,
				spawnPos.y + 0.2f,
				spawnPos.z + (rand() % 11 - 5) * 0.15f
			};
			Vector3 debrisVel = {
				(rand() % 11 - 5) * 0.04f,
				0.35f + (rand() % 8) * 0.02f,
				(rand() % 11 - 5) * 0.04f
			};
			Vector4 debrisColor = (rand() % 2 == 0) ? Vector4{ 0.6f, 0.05f, 0.0f, 0.85f } : Vector4{ 0.3f, 0.0f, 0.5f, 0.8f };
			float debrisScale = 1.0f + (rand() % 6) * 0.2f;
			GPUParticleManager::GetInstance()->Emit(debrisPos, debrisVel, 1.0f, debrisScale, debrisColor);
		}
'@

if ($content.Contains($oldBlock)) {
    $newContent = $content.Replace($oldBlock, $newBlock)
    # CRLF に戻してから書き出し
    $newContent = $newContent.Replace("`n", "`r`n")
    [System.IO.File]::WriteAllText($path, $newContent, [System.Text.Encoding]::UTF8)
    Write-Host "SUCCESS: minion entrance particles replaced"
} else {
    Write-Host "ERROR: old block not found - checking partial match..."
    # 最初の数行だけ検索してデバッグ
    $firstLine = "		// 1 " + [char]0x2460 + " "
    Write-Host "Contains check for aura line: $($content.Contains('for (int j = 0; j < 40; j++)'))"
    Write-Host "Contains check for ringAngle: $($content.Contains('float ringAngle = (3.141592f * 2.0f / 20.0f) * j;'))"
}
