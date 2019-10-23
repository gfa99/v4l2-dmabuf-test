#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <string.h>
#include <malloc.h>
#include <linux/fb.h>

/**
 ** v4l2在帧缓存区预览摄像头: gcc -std=c99 -o main main.c
 **/
static int width;//屏幕宽度
static int height;//屏幕高度
static __u32 *pfb;//屏幕缓冲区指针
static int fb;
static struct fb_fix_screeninfo finfo;
static struct fb_var_screeninfo vinfo;
void fb_fillimg(const __u32* img);

void initFb()
{
    int ret = -1;
    //打开帧缓冲区设备
    fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {  perror("open fb0"); return;  }
    printf("open /dev/fb0 success \n");
    //得到固定屏幕信息
    ret = ioctl(fb,FBIOGET_FSCREENINFO,&finfo);
    //得到可变屏幕信息
    ret = ioctl(fb, FBIOGET_VSCREENINFO, &vinfo);
    if (ret < 0) { perror("get var info"); return; }
    width = vinfo.xres_virtual;
    height = vinfo.yres_virtual;
    printf("width=%d,height=%d\n",width,height);
    //映射帧缓冲区基址
    pfb = (__u32*)mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (NULL == pfb) { perror("fb0 mmap"); return; }
}

void closeFb()
{
    munmap(pfb,finfo.smem_len);
    close(fb);
}

/**
 ** yuv422转bgra
 **/
int max(int a,int b){ return a<b? b:a; }
int min(int a,int b){ return a<b? a:b; }

__u32 yuv2rgb(int Y0,int U0,int Y1,int V1,int index)
{
    int Y = index? Y1:Y0;
    int R0 = max(0,min(255,1.164*(Y-16) + 1.596*(V1-128)));
    int G0 = max(0,min(255,1.164*(Y-16) - 0.391*(U0-128) - 0.813*(V1-128)));
    int B0 = max(0,min(255,1.164*(Y-16) + 2.018*(U0-128)));
    return (0x00<<24) | (R0<<16) | (G0<<8) | (B0<<0);//转为bgra
}

int main()
{
    int fd;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    __u8* bufStart;//图形缓冲区基址
    enum v4l2_buf_type type;
    //打开摄像头设备
    fd = open("/dev/video0", O_RDWR);
    if (fd<0) { perror("open video0"); return -1;}
    /**
     ** 设置摄像头格式，我的UVC摄像头有两个格式：YUV422(YUYV)和MJPEG
     ** 我的摄像头宽高是640x480
     **/
    memset( &fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;//
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("set format failed\n");return -1; }
    //得到实际的格式
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) { perror("get format failed\n");return -1; }
    printf("Picture:Width = %d   Height = %d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
    //申请缓冲区
    memset(&req, 0, sizeof (req));
    req.count = 1; //缓冲区数量为1
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;//mmap方式
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) { perror("request buffer error \n"); return 0-1; }
    //缓冲区类型设置
    memset( &buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("VIDIOC_QUERYBUF error\n"); return -1;}
 
    printf("buffer len=%u\n",buf.length);//长度614400=640x480x2
    /** 映射得到缓冲区的地址 **/
    bufStart = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,MAP_SHARED, fd, buf.m.offset);
    if (bufStart == MAP_FAILED)
    {
        perror("buffers mmap\n");
        return -1;
    }
    /** 将缓冲区放入到循环队列**/
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) { perror("VIDIOC_QBUF error\n"); return -1; }
    /** 打开流：stream on，打开流后摄像头驱动就开始往缓冲区缓冲区队列中的缓冲区填入数据了 **/
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("VIDIOC_STREAMON error\n"); return -1;}
 
    int i=0;
    //定义转换后的bgra区域
        #define OUT_SIZE 640*480*4
        __u32 *out = (__u32*)malloc(OUT_SIZE);//bgra
        __u32 *saveout;
        if(out==NULL)  { perror("out"); return -1;}
        __u8 *saveBufStart;
        initFb();//初始化帧缓冲区
    while(i++<100){  //循环读取500帧
        //从循环队列中取出缓冲区
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) { perror("VIDIOC_DQBUF failed.\n"); return -1; }
 
         //将yuv422转为bgra
            saveBufStart = bufStart;
            saveout=out;
            for(int t = 0;t<OUT_SIZE/2;t+=4){
                *out++ = yuv2rgb(bufStart[0],bufStart[1],bufStart[2],bufStart[3],0);
                *out++ = yuv2rgb(bufStart[0],bufStart[1],bufStart[2],bufStart[3],1);
                bufStart += 4;
            }
            out = saveout;
            bufStart = saveBufStart;
         //将bgra数据显示到帧缓冲区
            fb_fillimg(out);
 
        /** 将缓冲区重新放入循环队列**/
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) { perror("VIDIOC_QBUF error\n"); return -1;}
    }
 
    closeFb();
    munmap(bufStart,buf.length);
    close(fd);
    return 0;
}
/**
 ** 在帧缓冲区居中显示图片
 **/
#define imgW 640
#define imgH 480
void fb_fillimg(const __u32* img)
{
    unsigned int x, y,cX,cY;
    cX = (width-imgW)/2;
    cY = (height-imgH)/2;
    for (y = cY; y < cY+imgH; y++)
    {
        for (x = cX; x < cX+imgW; x++)
        {
            *(pfb + y * width + x) = *(img + (y-cY)*imgW+(x-cX));
        }
    }
 
}
/*
————————————————
版权声明：本文为CSDN博主「hello_zard」的原创文章，遵循 CC 4.0 BY-SA 版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/a694543965/article/details/80375991
zzl注：在Ubuntu16.04x64中可以正常测试成功，但需要先切到tty1~tty6(Ctrl+Alt+F1)下运行，即可实时显示摄像头捕获的图像 */

int get_screeninfo()
{
    // 原文链接：https://blog.csdn.net/aiwangtingyun/article/details/79456789

    int fd;

    /* 打开fb设备文件 */
    fd = open("/dev/fb0", O_RDWR);
    if (-1 == fd)
    {
        perror("open fb");
        return -1;
    }

    /* 获取fix屏幕信息:获取命令为FBIOGET_FSCREENINFO */
    struct fb_fix_screeninfo fixInfo;

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fixInfo) == -1)
    {
        perror("get fscreeninfo");
        close(fd);
        return -2;
    }
    /* 打印fix信息 */
    printf("id = %s\n", fixInfo.id); /* 厂商id信息 */
    printf("line length = %d\n", fixInfo.line_length); 
                                    /* 这里获取的是一行像素所需空间
                                     * 该空间大小是出厂时就固定的了
                                     * 厂商会对一行像素字节进行对齐*/

    /* 获取var屏幕的信息:获取命令为FBIOGET_VSCREENINFO */
    struct fb_var_screeninfo varInfo;

    if (ioctl(fd, FBIOGET_VSCREENINFO, &varInfo) == -1)
    {
        perror("get var screen failed\n");
        close(fd);
        return -3;
    }
    /* 打印var信息 */
    printf("xres = %d, yres = %d\n", varInfo.xres, varInfo.yres);
    printf("bits_per_pixel = %d\n", varInfo.bits_per_pixel);
    printf("red: offset = %d, length = %d\n", \
                    varInfo.red.offset, varInfo.red.length);
    printf("green: offset = %d, length = %d\n", \
                    varInfo.green.offset, varInfo.green.length);
    printf("blue: offset = %d, length = %d\n", \
                    varInfo.blue.offset, varInfo.blue.length);
    printf("transp: offset = %d, length = %d\n", \
                    varInfo.transp.offset, varInfo.transp.length);

    close(fd);
    return 0;
}
