#include <stdio.h>
#include <stdlib.h>

// 定义YUV数据结构
typedef struct {
    unsigned char* y;
    unsigned char* u;
    unsigned char* v;
} YUV;

// 双线性插值函数
unsigned char bilinearInterpolation(const unsigned char* src, int srcWidth, int srcHeight, float x, float y) {
    int x1 = (int)x;
    int y1 = (int)y;
    int x2 = x1 + 1 < srcWidth ? x1 + 1 : x1;
    int y2 = y1 + 1 < srcHeight ? y1 + 1 : y1;

    unsigned char Q11 = src[y1 * srcWidth + x1];
    unsigned char Q12 = src[y2 * srcWidth + x1];
    unsigned char Q21 = src[y1 * srcWidth + x2];
    unsigned char Q22 = src[y2 * srcWidth + x2];

    float R1 = ((x2 - x) / (x2 - x1)) * Q11 + ((x - x1) / (x2 - x1)) * Q21;
    float R2 = ((x2 - x) / (x2 - x1)) * Q12 + ((x - x1) / (x2 - x1)) * Q22;

    return (unsigned char)(((y2 - y) / (y2 - y1)) * R1 + ((y - y1) / (y2 - y1)) * R2);
}

// YUV444放大函数
YUV enlargeYUV444(const YUV* src, int srcWidth, int srcHeight, int dstWidth, int dstHeight) {
    YUV dst;
    dst.y = (unsigned char*)malloc(dstWidth * dstHeight);
    dst.u = (unsigned char*)malloc(dstWidth * dstHeight);
    dst.v = (unsigned char*)malloc(dstWidth * dstHeight);

    float scaleX = (float)srcWidth / dstWidth;
    float scaleY = (float)srcHeight / dstHeight;

    for (int j = 0; j < dstHeight; j++) {
        for (int i = 0; i < dstWidth; i++) {
            dst.y[j * dstWidth + i] = bilinearInterpolation(src->y, srcWidth, srcHeight, i * scaleX, j * scaleY);
            dst.u[j * dstWidth + i] = bilinearInterpolation(src->u, srcWidth, srcHeight, i * scaleX, j * scaleY);
            dst.v[j * dstWidth + i] = bilinearInterpolation(src->v, srcWidth, srcHeight, i * scaleX, j * scaleY);
        }
    }

    return dst;
}

// 读取YUV文件
YUV readYUV(const char* filename, int width, int height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        exit(1);
    }

    YUV yuv;
    yuv.y = (unsigned char*)malloc(width * height);
    yuv.u = (unsigned char*)malloc(width * height);
    yuv.v = (unsigned char*)malloc(width * height);

    fread(yuv.y, 1, width * height, file);
    fread(yuv.u, 1, width * height, file);
    fread(yuv.v, 1, width * height, file);

    fclose(file);

    return yuv;
}

// 写入YUV文件
void writeYUV(const char* filename, const YUV* yuv, int width, int height) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        exit(1);
    }

    fwrite(yuv->y, 1, width * height, file);
    fwrite(yuv->u, 1, width * height, file);
    fwrite(yuv->v, 1, width * height, file);

    fclose(file);
}

// 测试main函数
int main() {
    // 读取YUV444文件
    YUV yuv = readYUV("yuv444p.yuv", 690, 388);

    // 放大YUV444
    YUV enlargedYUV = enlargeYUV444(&yuv, 690, 388, 690*2, 388*2);

    // 写入YUV444文件
    writeYUV("output.yuv", &enlargedYUV, 690*2, 388*2);

    // 释放内存
    free(yuv.y);
    free(yuv.u);
    free(yuv.v);
    free(enlargedYUV.y);
    free(enlargedYUV.u);
    free(enlargedYUV.v);

    return 0;
}