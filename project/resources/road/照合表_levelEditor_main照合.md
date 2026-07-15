# 照合表: levelEditor_main × これまでの依頼・仕様

読んだリポジトリ: github.com/yuzurihakazuma/KazumimiEngine (levelEditor_main)
最終コミット: 2026-07-10 15:45 「道の修正中」
※以前のSDFmain照合は破棄。本表が最新。

## A. 実装済み (設計書・仕様書どおり確認できたもの)

| # | 項目 | 実装場所 (ファイル:行) | 備考 |
|---|---|---|---|
| 1 | FrameCache (RailFrame + GetFrameAtDistance) | SplineRail.h:77-97 | 設計書§3の名前そのまま |
| 2 | 道の掃引生成 (レール1本=1メッシュ) | RoadMesh.cpp:103〜 BuildRailMesh | 12頂点断面・u=距離/2 |
| 3 | 曲率適応サンプリング | RoadMesh.cpp:107〜 | ガイドどおり |
| 4 | 内側折返しの溶接 | RoadMesh.cpp:148 (ringPos保存) | ✓ |
| 5 | 坂UV切替 + ヒステリシス + 二重リング | RoadMesh.cpp:44-131 (kSlopeV, 0.45/0.35) | ✓ |
| 6 | 穴区間の隙間 + 切り口のフタ | RoadMesh.cpp:181-235 (Seg::Hole, フタ) | 仕様の「road_end丸キャップ+警告帯」とは別解。フタ方式でも成立 |
| 7 | 交差点の自動配置 (T/十字 + コーナー掃引) | RoadMesh.cpp CollectJunctions + Cut機構 | **90°軸沿い限定** (ヘッダ:19に明記) |
| 8 | 自由端に road_end 自動配置 | RoadMesh.h:20 / PlaceEndCap | ✓ |
| 9 | 動くレール追従 (再生成なし) | RoadMesh.h:21 / Update + animOffset | 設計書§6どおり |
| 10 | アセット導入 | resources/road/ (road_atlas.png + 各.obj) | **v3/v4世代**。ドキュメントも同梱済み |
| 11 | 穴のデータ/判定/赤マーカー | SplineRail::nodeHole ほか (前回同様) | ✓ |
| 12 | Undo/Redo・手動溶接/連結 | RailEditor (端点を溶接 :1109 / ConnectNearbyLines) | ✓ |

## B. 未実装 (= 残タスク。仕様書_残タスク_levelEditor_main版.md が対応)

| # | 項目 | 現状 | 対応節 |
|---|---|---|---|
| 13 | 編集中が重い | **原因2つを行番号で特定**: (a) GamePlayScene.cpp:283-284 が編集のたび Sync+Build を全レールで実行 / (b) RailField.cpp:357〜 マーカーが0.5m毎のObj3d群 / (c) RoadMesh::Build が毎回 Model::InitializePrimitive で頂点リソースを作り直し | §1 (最優先) |
| 14 | 任意角度のジャンクション | 90°軸沿いタイル+掃引コネクタのみ (「仮置き」の正体) | §2 |
| 15 | 危険帯 (穴の警告テクスチャ) | 無し (アトラスがv4世代で帯自体が無い) | §3 |
| 16 | v5アセット (危険帯入り) | resources/road は旧世代 | §3 (atlas+全objを**同時**差し替え) |
| 17 | レール毎の道ON/OFF | RoadMesh::SetVisible の全体切替のみ | §4 |
| 18 | 自動スナップ接続 | 手動ボタンのまま | §5 |
| 19 | ジョイント表示 (プラレール風) | 無し (road_joint.obj は同梱済) | §5 |

## C. 注意点
- **アセットはOBJ運用** (Model::InitializePrimitive / ModelManager)。zip内の各ピースは
  .obj も同梱済みなのでそのまま置ける。スキンモデル(road_skinned_*.glb)だけは
  GLBのみ → SkinnedObj3d の対応形式を要確認 (使う段になってから)
- v4→v5 はモデル用UV領域を移動しているため、**atlasだけ差し替えると交差点ピースの
  見た目が壊れる**。road_atlas.png と road_*.obj を必ずセットで入れ替えること
- 設計書の名称対応: FrameCache→SplineRail::frameCache_ / RoadMeshBuilder→RoadMesh /
  GetFrameAtDistance→同名で実装済み
