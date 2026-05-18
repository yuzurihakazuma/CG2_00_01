#include "Matrix4x4.h"
#include "engine/math/QuaternionMath.h"
#include "VectorMath.h"

#include <cmath>
#include <cassert>


namespace MatrixMath {

    // 行列の加法
    Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) {
        Matrix4x4 result = {};
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                result.m[row][col] = m1.m[row][col] + m2.m[row][col];
            }
        }
        return result;
    }

    // 行列の減法
    Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
        Matrix4x4 result = {};
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                result.m[row][col] = m1.m[row][col] - m2.m[row][col];
            }
        }
        return result;
    }

    // 4x4行列の積
    Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
        Matrix4x4 result = {};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                for (int k = 0; k < 4; ++k) {
                    result.m[row][col] += m1.m[row][k] * m2.m[k][col];
                }
            }
        }
        return result;
    }

    // 4x4行列の逆行列
    Matrix4x4 Inverse(const Matrix4x4& m) {
        float aug[4][8] = {};
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                aug[row][col] = m.m[row][col];
            }
        }
        aug[0][4] = 1.0f; aug[1][5] = 1.0f; aug[2][6] = 1.0f; aug[3][7] = 1.0f;

        for (int i = 0; i < 4; i++) {
            if (aug[i][i] == 0.0f) {
                for (int j = i + 1; j < 4; j++) {
                    if (aug[j][i] != 0.0f) {
                        for (int k = 0; k < 8; k++) {
                            float copyNum = aug[i][k];
                            aug[i][k] = aug[j][k];
                            aug[j][k] = copyNum;
                        }
                        break;
                    }
                }
            }
            float pivot = aug[i][i];
            for (int k = 0; k < 8; k++) {
                aug[i][k] /= pivot;
            }
            for (int j = 0; j < 4; j++) {
                if (j != i) {
                    float factor = aug[j][i];
                    for (int k = 0; k < 8; k++) {
                        aug[j][k] -= factor * aug[i][k];
                    }
                }
            }
        }

        Matrix4x4 result = {};
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                result.m[row][col] = aug[row][col + 4];
            }
        }
        return result;
    }

    // 転置行列
    Matrix4x4 Transpose(const Matrix4x4& m) {
        Matrix4x4 result = {};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                result.m[row][col] = m.m[col][row];
            }
        }
        return result;
    }

    // 単位行列の作成
    Matrix4x4 MakeIdentity4x4() {
        Matrix4x4 result = {};
        for (int i = 0; i < 4; ++i) {
            result.m[i][i] = 1.0f;
        }
        return result;
    }

    // 平行移動行列
    Matrix4x4 MakeTranslate(const Vector3& translate) {
        Matrix4x4 result = MakeIdentity4x4();
        result.m[3][0] = translate.x;
        result.m[3][1] = translate.y;
        result.m[3][2] = translate.z;
        return result;
    }

    // 拡大縮小行列
    Matrix4x4 MakeScale(const Vector3& scale) {
        Matrix4x4 result = MakeIdentity4x4();
        result.m[0][0] = scale.x;
        result.m[1][1] = scale.y;
        result.m[2][2] = scale.z;
        return result;
    }

    // X軸の回転行列
    Matrix4x4 MakeRotateX(float radian) {
        Matrix4x4 result = MakeIdentity4x4();
        result.m[1][1] = std::cos(radian);
        result.m[1][2] = std::sin(radian);
        result.m[2][1] = -std::sin(radian);
        result.m[2][2] = std::cos(radian);
        return result;
    }

    // Y軸の回転行列
    Matrix4x4 MakeRotateY(float radian) {
        Matrix4x4 result = MakeIdentity4x4();
        result.m[0][0] = std::cos(radian);
        result.m[0][2] = -std::sin(radian);
        result.m[2][0] = std::sin(radian);
        result.m[2][2] = std::cos(radian);
        return result;
    }

    // Z軸の回転行列
    Matrix4x4 MakeRotateZ(float radian) {
        Matrix4x4 result = MakeIdentity4x4();
        result.m[0][0] = std::cos(radian);
        result.m[0][1] = std::sin(radian);
        result.m[1][0] = -std::sin(radian);
        result.m[1][1] = std::cos(radian);
        return result;
    }

	// forwardとupから回転行列を作る関数
    Matrix4x4 MakeDirectionRotation(const Vector3& forward, const Vector3& up){

		// forwardをz軸、upをy軸とする回転行列を作る
		Vector3 z = VectorMath::Normalize(forward);
		// upをy軸とするために、upとzの外積を取ってx軸を求める
		Vector3 x = VectorMath::Normalize(VectorMath::Cross(up, z));
		
		// zとxの外積を取ってy軸を求める
        Vector3 y = VectorMath::Normalize(VectorMath::Cross(z, x));
        
		// x、y、zを回転行列の列ベクトルとして配置する
		Matrix4x4 result = MakeIdentity4x4();
      

		// 注意：行列の配置は列ベクトル形式で行うため、x、y、zをそれぞれの列に配置する
		result.m[0][0] = x.x; result.m[0][1] = x.y; result.m[0][2] = x.z;
		result.m[1][0] = y.x; result.m[1][1] = y.y; result.m[1][2] = y.z;
		result.m[2][0] = z.x; result.m[2][1] = z.y; result.m[2][2] = z.z;
        
		// 最後に、回転行列を転置して、行ベクトル形式に変換する
		return result;



    }

    // 3次元アフィン変換行列
    Matrix4x4 MakeAffine(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
        Matrix4x4 scaleMatrix = MakeScale(scale);
        Matrix4x4 rotateXMatrix = MakeRotateX(rotate.x);
        Matrix4x4 rotateYMatrix = MakeRotateY(rotate.y);
        Matrix4x4 rotateZMatrix = MakeRotateZ(rotate.z);
        Matrix4x4 rotateXYZMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
        Matrix4x4 translateMatrix = MakeTranslate(translate);
        return Multiply(Multiply(scaleMatrix, rotateXYZMatrix), translateMatrix);
    }

	// Quaternion を使ったアフィン変換行列
    Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
        Matrix4x4 scaleMatrix = MakeScale(scale);
        Matrix4x4 rotateMatrix = QuaternionMath::MakeRotateMatrix(rotate); // Quaternion → 回転行列
        Matrix4x4 translateMatrix = MakeTranslate(translate);
        return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
    }

    // 座標変換
    Vector3 Transforms(const Vector3& vector, const Matrix4x4& matrix) {
        Vector3 result = {};
        result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
        result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
        result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];
        float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];
        assert(w != 0.0f);
        result.x /= w; result.y /= w; result.z /= w;
        return result;
    }

    // 正射影行列
    Matrix4x4 Orthographic(float left, float top, float right, float bottom, float nearClip, float farClip) {
        Matrix4x4 result = {};
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = 2.0f / (top - bottom);
        result.m[2][2] = 1.0f / (farClip - nearClip);
        result.m[3][0] = (left + right) / (left - right);
        result.m[3][1] = (top + bottom) / (bottom - top);
        result.m[3][2] = nearClip / (nearClip - farClip);
        result.m[3][3] = 1.0f;
        return result;
    }

    // 透視投影行列
    Matrix4x4 PerspectiveFov(float fovY, float aspectRatio, float nearClip, float farClip) {
        Matrix4x4 result = {};
        float f = 1.0f / std::tan(fovY / 2.0f);
        result.m[0][0] = f / aspectRatio;
        result.m[1][1] = f;
        result.m[2][2] = farClip / (farClip - nearClip);
        result.m[2][3] = 1.0f;
        result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
        return result;
    }

    // ビューポート変換行列
    Matrix4x4 Viewport(float left, float top, float width, float height, float minDepth, float maxDepth) {
        Matrix4x4 result = {};
        result.m[0][0] = width / 2.0f;
        result.m[1][1] = -height / 2.0f;
        result.m[2][2] = maxDepth - minDepth;
        result.m[3][0] = left + (width / 2.0f);
        result.m[3][1] = top + (height / 2.0f);
        result.m[3][2] = minDepth;
        result.m[3][3] = 1.0f;
        return result;
    }

} // namespace MatrixMath