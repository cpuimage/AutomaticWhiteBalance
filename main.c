
#include "browse.h"

#define USE_SHELL_OPEN

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"

#define  SFD_IMPLEMENTATION

#include "sfd.h"
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include "timing.h"
#include <stdint.h>

#ifndef ClampToByte
#define ClampToByte(v)(((unsigned) (int)(v)) < (255) ? (v) : (v < 0) ? (int)(0) : (int)(255))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif


unsigned char *loadImage(const char *filename, int *Width, int *Height, int *Channels) {
    return (stbi_load(filename, Width, Height, Channels, 0));
}


void saveImage(const char *filename, int Width, int Height, int Channels, unsigned char *Output) {

    if (!stbi_write_jpg(filename, Width, Height, Channels, Output, 100)) {
        fprintf(stderr, "save JPEG fail.\n");
        return;
    }
#ifdef USE_SHELL_OPEN
    browse(filename);
#endif
}


void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
    const char *end;
    const char *p;
    const char *s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    } else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}

void
rgb2ycbcr(unsigned char R, unsigned char G, unsigned char B, unsigned char *y, unsigned char *cb, unsigned char *cr) {
    *y = (unsigned char) ((19595 * R + 38470 * G + 7471 * B) >> 16);
    *cb = (unsigned char) (((36962 * (B - *y)) >> 16) + 128);
    *cr = (unsigned char) (((46727 * (R - *y)) >> 16) + 128);
}


void
autoLevel(const unsigned int *histgram, unsigned char *remapLut, int numberOfPixels, float cutLimit, float contrast) {
    int minPos = 0, maxPos = 255;
    int minValue = 0, maxValue = 255;
    for (int I = 0; I < 256; I++) {
        if (histgram[I] != 0) {
            minValue = I;
            break;
        }
    }
    for (int I = 255; I >= 0; I--) {
        if (histgram[I] != 0) {
            maxValue = I;
            break;
        }
    }
    int sum = 0;
    for (int I = minValue; I < 256; I++) {
        sum = sum + histgram[I];
        if (sum >= numberOfPixels * cutLimit) {
            minPos = I;
            break;
        }
    }
    sum = 0;
    for (int I = 255; I >= 0; I--) {
        sum = sum + histgram[I];
        if (sum >= numberOfPixels * cutLimit) {
            maxPos = I;
            break;
        }
    }

    int delta = (int) ((maxValue - minValue) * contrast * 0.5f);
    minValue = ClampToByte(minValue - delta);
    maxValue = ClampToByte(maxValue + delta);
    if (maxPos != minPos) {
        for (int I = 0; I < 256; I++) {
            if (I < minPos)
                remapLut[I] = (unsigned char) minValue;
            else if (I > maxPos)
                remapLut[I] = (unsigned char) maxValue;
            else
                remapLut[I] = (unsigned char) ClampToByte(
                        (maxValue - minValue) * (I - minPos) / (maxPos - minPos) + minValue);

        }
    } else {
        for (int I = 0; I < 256; I++) {
            remapLut[I] = (unsigned char) maxPos;
        }
    }
}

bool isColorCast(const unsigned int *histogramCb, const unsigned int *histogramCr, int numberOfPixels, int colorCoeff) {
    unsigned int sumCb = 0;
    unsigned int sumCr = 0;
    float meanCb = 0, meanCr = 0;
    for (unsigned int i = 0; i < 256; i++) {
        sumCb += histogramCb[i] * i;
        sumCr += histogramCr[i] * i;
    }
    meanCb = sumCb * (1.0f / numberOfPixels);
    meanCr = sumCr * (1.0f / numberOfPixels);
    int avgColorCoeff = (abs(meanCb - 127) + abs(meanCr - 127));
    if (avgColorCoeff < colorCoeff) {
        return false;
    }
    return true;
}


bool autoWhiteBalance(unsigned char *input, unsigned char *output, int width, int height, int channels, int stride,
                      int colorCoeff, float cutLimit,
                      float contrast) {
    bool ret = false;
    if (channels == 3 || channels == 4) {
        int numberOfPixels = height * width;
        unsigned int histogramYcbcr[768] = {0};
        unsigned int *histogramY = &histogramYcbcr[0];
        unsigned int *histogramCb = &histogramYcbcr[256];
        unsigned int *histogramCr = &histogramYcbcr[512];
        unsigned int histogramRGB[768] = {0};
        unsigned int *histogramR = &histogramRGB[0];
        unsigned int *histogramG = &histogramRGB[256];
        unsigned int *histogramB = &histogramRGB[512];
        unsigned char Y = 0;
        unsigned char Cb = 0;
        unsigned char Cr = 0;
        for (int y = 0; y < height; y++) {
            const unsigned char *scanIn = input + y * stride;
            for (int x = 0; x < width; x++) {
                const unsigned char R = scanIn[0];
                const unsigned char G = scanIn[1];
                const unsigned char B = scanIn[2];
                histogramR[R]++;
                histogramG[G]++;
                histogramB[B]++;
                rgb2ycbcr(R, G, B, &Y, &Cb, &Cr);
                histogramY[Y]++;
                histogramCb[Cb]++;
                histogramCr[Cr]++;
                scanIn += channels;
            }
        }
        ret = isColorCast(histogramCb, histogramCr, numberOfPixels, colorCoeff);
        if (!ret) {
            memcpy(output, input, numberOfPixels
                                  * channels * sizeof(*input));
            return ret;
        }
        unsigned char mapRGB[256 * 3] = {0};
        unsigned char *mapR = &mapRGB[0];
        unsigned char *mapG = &mapRGB[256];
        unsigned char *mapB = &mapRGB[256 + 256];
        autoLevel(histogramR, mapR, numberOfPixels, cutLimit, contrast);
        autoLevel(histogramG, mapG, numberOfPixels, cutLimit, contrast);
        autoLevel(histogramB, mapB, numberOfPixels, cutLimit, contrast);
        for (int y = 0; y < height; y++) {
            unsigned char *scanIn = input + y * stride;
            unsigned char *scanOut = output + y * stride;
            for (int x = 0; x < width; x++) {
                scanOut[0] = mapR[scanIn[0]];
                scanOut[1] = mapG[scanIn[1]];
                scanOut[2] = mapB[scanIn[2]];
                scanIn += channels;
                scanOut += channels;
            }
        }
    }
    return ret;
}


int main(int argc, char **argv) {
    printf("Automatic White Balance Algorithm Implementation In C\n ");
    printf("blog:http://cpuimage.cnblogs.com/ \n ");
    char *filename = NULL;
    if (argc < 2) {
        printf("usage: %s   image \n ", argv[0]);
        printf("eg: %s   d:\\image.jpg \n ", argv[0]);
        sfd_Options opt = {
                .title        = "Open Image File",
                .filter_name  = "Image File",
                .filter       = "*.png|*.jpg",
        };

        filename = (char *) sfd_open_dialog(&opt);

        if (filename) {
            printf("Got file: '%s'\n", filename);
        } else {
            printf("Open canceled\n");
            return -1;
        }

    } else
        filename = argv[1];
    char drive[3];
    char dir[256];
    char fname[256];
    char ext[256];
    char out_file[1024];
    splitpath(filename, drive, dir, fname, ext);
    sprintf(out_file, "%s%s%s_out.jpg", drive, dir, fname);

    int Width = 0;
    int Height = 0;
    int Channels = 0;
    unsigned char *inputImage = NULL;
    double startTime = now();
    inputImage = loadImage(filename, &Width, &Height, &Channels);
    double nLoadTime = calcElapsed(startTime, now());
    printf("load time: %d ms.\n ", (int) (nLoadTime * 1000));
    if ((Channels != 0) && (Width != 0) && (Height != 0)) {
        unsigned char *outputImg = (unsigned char *) stbi__malloc(Width * Channels * Height * sizeof(unsigned char));
        if (inputImage == NULL || outputImg == NULL) {
            fprintf(stderr, "load: %s fail!\n ", filename);
            return -1;
        }

        int colorCoeff = 15;//def:15 [0,127]
        float cutLimit = 0.01;//def:0.01  [0.0,1.0]
        float contrast = 0.9;//def:0.9 [0.0,1.0]
        startTime = now();
        bool colorCast = autoWhiteBalance(inputImage, outputImg, Width, Height, Channels, Width * Channels, colorCoeff,
                                          cutLimit,
                                          contrast);
        if (colorCast) {
            printf("[√]ColorCast \n");
        } else {
            printf("[x]ColorCast \n");
        }
        double nProcessTime = calcElapsed(startTime, now());
        printf("process time: %d ms.\n ", (int) (nProcessTime * 1000));
        startTime = now();
        saveImage(out_file, Width, Height, Channels, outputImg);
        double nSaveTime = calcElapsed(startTime, now());
        printf("save time: %d ms.\n ", (int) (nSaveTime * 1000));
        stbi_image_free(outputImg);
        stbi_image_free(inputImage);
    } else {
        fprintf(stderr, "load: %s fail!\n", filename);
    }
    printf("press any key to exit. \n");
    getchar();
    return (EXIT_SUCCESS);
}