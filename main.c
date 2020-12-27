#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

/*
 * Изображения в формате pbm - это текстовый файл из 0 и 1, где 1 считается чёрным пикселем, а 0 - белым.
 * не все программы для отображения картинок знают такой формат, но можно открыть их как текстовый файл.
 * Подобрал картинки так, чтобы в текстовом виде было не сложно увидеть результат
 * aperture48 - это картинка 48х48 пикселей (картинка получена из файла aperture reference.png)
 * aperture1920 - это сетка из 400 aperture48 (то есть 20х20 таких картинок по 48х48)
 */
typedef struct {
    int width;
    int height;
    char* pixels;
} PBMImage;

PBMImage*  CreateImage(int height, int width)
{
    PBMImage* img = (PBMImage*)malloc(sizeof(PBMImage));
    img->height = height;
    img->width = width;
    img->pixels = (char *)calloc(img->height * img->width, sizeof(char));
    return img;
}

void setPixel(PBMImage* img, int row, int column, int value)
{
    if (row < 0 || row >= img->height )
        return;
    if (column < 0 || column >= img->width)
        return;
    img->pixels[row * img->width + column] = value;
}


PBMImage* readImage(char* path)
{
    PBMImage* img;
    int width, height;
    char buff[16];
    FILE* fp = fopen(path, "rt");

    //read image format
    if (!fgets(buff, sizeof(buff), fp)) {
        perror(path);
        exit(1);
    }

    //check the image format
    if (buff[0] != 'P' || buff[1] != '1') {
        fprintf(stderr, "Invalid image format (must be 'P1')\n");
        exit(1);
    }

    //read image size information
    if (fscanf(fp, "%d %d", &width, &height) != 2) {
        fprintf(stderr, "Invalid image size (error loading '%s')\n", path);
        exit(1);
    }
    img = CreateImage(height, width);

    //read pixels
    char c;
    for (int i = 0; i < img->height; ++i) {
        for (int j = 0; j < img->width; ++j) {
            c = getc(fp);
            if (c == '\n')
                c = getc(fp);
            if(c == '1')
                img->pixels[i*img->width + j] = 1;
            else
                img->pixels[i*img->width + j] = 0;
        }
    }

    return img;
}

void writeImage(PBMImage* img, char* path)
{
    FILE *fo = fopen(path, "wt");
    fprintf(fo, "P1\n%d %d\n", img->width, img->height);
    for (int i = 0; i < img->height; ++i) {
        for (int j = 0; j < img->width; ++j) {
            fprintf(fo, "%d", img->pixels[i*img->width + j]);
        }
        fprintf(fo,"\n");
    }
    fclose(fo);
}

// создаёт сетку размером times х times из картинки.
// использовалось для генерации aperture1920 (это aperture48 20х20)
PBMImage multitile(PBMImage* img, int times)
{
    int sw = img->width; // source
    int dw = sw * times; // dest
    int sh = img->height; // source
    int dh = sh * times; // dest

    char * pixels = (char *)malloc(sizeof(char) * dw * dh);

    for (int i = 0; i < sh; ++i) {
        for (int k = 0; k < times; ++k) {
            for (int j = 0; j < times; ++j) {
                memcpy(pixels + sh*dw*k + i * dw + j * sw, img->pixels + i * sw, sw);
            }
        }
    }
    free(img->pixels);
    img->pixels = pixels;
    img->width *= times;
    img->height *= times;
}


/*
 * Алгоритм дилатации следующий - для каждого пикселя исходного изображения проверяем что он чёрный,
 * если это так, то в результирующем изображении для этого пикселя все соседние становятся чёрными.
 * (алгоритм эквивалентен дилатации с маской 2х2)
 */
void dilate(PBMImage* imgSrc, PBMImage* imgDst)
{
    for (int i = 0; i < imgSrc->height; ++i) {
        for (int j = 0; j < imgSrc->width; ++j) {
            if(imgSrc->pixels[i * imgSrc->width + j] == 1)
            {
                 setPixel(imgDst, i,j+1,1);
                 setPixel(imgDst, i,j-1,1);
                 setPixel(imgDst, i-1,j+1,1);
                 setPixel(imgDst, i-1,j-1,1);
                 setPixel(imgDst, i+1,j+1,1);
                 setPixel(imgDst, i+1,j-1,1);
            }
        }
    }
}


/*
 * Многопоточная дилатация - тот же алгоритм выполняется в нескольких потоках.
 * Изображение по высоте делится на блоки, количество блоков равно количеству потоков.
 * Каждый поток обрабатывает один блок.
 */

typedef struct {
    int startHeight;
    int endHeight;
    PBMImage* imgSrc;
    PBMImage* imgDst;
} ThreadData;

void threadFunc(LPVOID lpParam)
{
    ThreadData * data = (ThreadData*)lpParam;
    PBMImage* imgSrc = data->imgSrc;
    PBMImage* imgDst = data->imgDst;

    for (int i = data->startHeight; i < data->endHeight; ++i) {
        for (int j = 0; j < imgSrc->width; ++j) {
            if(imgSrc->pixels[i * imgSrc->width + j] == 1)
            {
                setPixel(imgDst, i,j+1,1);
                setPixel(imgDst, i,j-1,1);
                setPixel(imgDst, i-1,j+1,1);
                setPixel(imgDst, i-1,j-1,1);
                setPixel(imgDst, i+1,j+1,1);
                setPixel(imgDst, i+1,j-1,1);
            }
        }
    }
}

PBMImage dilate_threaded(PBMImage* imgSrc, PBMImage* imgDst, int threadsCount)
{
    // let max be 16 threads;
    DWORD *dwThreadId[16];
    HANDLE *hThreads[16];
    ThreadData data[16];
    int i;

    int threadBlockSize = imgSrc->height / threadsCount;

    for (i = 0; i < threadsCount; i++)
    {
        data[i].imgSrc = imgSrc;
        data[i].imgDst = imgDst;
        data[i].startHeight = i * threadBlockSize;
        data[i].endHeight = data[i].startHeight + threadBlockSize;

        hThreads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)threadFunc, (LPVOID)&data[i], CREATE_SUSPENDED, dwThreadId + i);
    }

    for (i = 0; i < threadsCount; i++)	// 	START THE THREADS PROCESSING
        ResumeThread(hThreads[i]);

    WaitForMultipleObjects(threadsCount, hThreads, TRUE, INFINITE);	// Wait end

    for (i = 0; i < threadsCount; i++)	// 	СLOSE THREADS
        CloseHandle(hThreads[i]);
}




/*
   Замерим время выполнения на разном числе потоков.
   Дилатация выполняется 100 раз, время усредняется.
   результаты для AMD Ryzen 5 2600 (6 ядер 12 потоков)

    dilate time:  0.061 sec - 1 thread
    dilate time:  0.030 sec - 2 threads
    dilate time:  0.016 sec - 4 threads
    dilate time:  0.012 sec - 6 threads
    dilate time:  0.010 sec - 8 threads
    dilate time:  0.008 sec - 10 threads
    dilate time:  0.007 sec - 12 threads
    dilate time:  0.009 sec - 14 threads
    dilate time:  0.009 sec - 16 threads
 */

int main() {
    PBMImage* img = readImage("images/aperture1920.pbm");
    PBMImage* dilatedImage = CreateImage(img->height, img->width);

    clock_t start, stop;
    start = clock();
    for (int i = 0; i < 100; ++i) {
        dilate(img, dilatedImage);
    }
    stop = clock();
    printf("dilate time: %6.3f sec - 1 thread\n", (double)(stop - start) / CLOCKS_PER_SEC / 100.0);

    for (int threadCount = 2; threadCount <= 16; threadCount+=2) {
        start = clock();
        for (int i = 0; i < 100; ++i) {
            dilate_threaded(img, dilatedImage, threadCount);
        }
        stop = clock();
        printf("dilate time: %6.3f sec - %d threads\n", (double)(stop - start) / CLOCKS_PER_SEC / 100.0, threadCount);
    }

    writeImage(dilatedImage, "images/aperture1920_dilated.pbm");
}



//    PBMImage img = readImage("images/yin-yang.pbm");
//    writeImage(&img, "images/yin-yang_dilated.pbm");