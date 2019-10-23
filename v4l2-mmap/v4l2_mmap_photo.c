#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
 
 
//定义缓冲区类型指针
typedef struct 
{
    //数据起始地址
    void *start;
    //数据长度
    int length;
}BUFTYPE; //定义缓冲区指针

BUFTYPE *usr_buf=NULL;

static unsigned int buf_num = 4;//指定缓冲区个数
 
static int fd;//打开的设备fd
/**
 * @brief init_camera 初始化相机设备属性
 * @param dev 设备名称
 * @return 成功返回0,失败返回-1
 */
int init_camera(const char* dev)
{
    fd = open(dev, O_RDWR);
    if(fd < 0){
        printf("open \"%s\" error\n", dev);
        return -1;
    }
 
    /**
     * 查询设备属性
     */
    struct v4l2_capability cap;
    int ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        printf("VIDIOC_QUERYCAP error\n");
        return -1;
    }
 
    printf("驱动名 : %s\n",cap.driver);
    printf("设备名字 : %s\n",cap.card);
    printf("总线信息 : %s\n",cap.bus_info);
    printf("驱动版本号 : %d\n",cap.version);
 
    if(cap.capabilities & V4L2_BUF_TYPE_VIDEO_CAPTURE){ /*判断是否为视频捕获设备*/
        printf("\n\t视频捕获设备:");
        if(cap.capabilities & V4L2_CAP_STREAMING){/*判断是否支持视频流捕获*/
            printf("支持视频流捕获\n");
        }else{
            printf("不支持视频流捕获\n");
        }
    }else {
        printf("非视频流捕获设备\n");
        return -1;
    }
 
    printf("查询支持的图像格式:\n");
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1)
    {
        printf("\t%d.%s\n",fmtdesc.index+1,fmtdesc.description);
        fmtdesc.index++;
    }
 
    /*设置格式*/
    struct v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//摄像头缓冲
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;//像素格式(需要根据实际设备情况设置该参数)
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        printf("设置格式:V4L2_PIX_FMT_MJPEG 失败\n");
        return -1;
    }
    return 0;
}
 
 
/**
 * @brief mmap_buffer 分配用户缓冲区内存,并建立内存映射
 * @return 成功返回0，失败返回-1
 */
int mmap_buffer()
{
    /*分配用户空间缓冲区*/
    usr_buf = (BUFTYPE*)calloc(buf_num, sizeof(BUFTYPE));
    if (!usr_buf) {
        printf("calloc \"frame buffer\" error : Out of memory\n");
        return -1;
    }
 
    /*分配内核缓冲区,包含帧缓冲区的数量*/
    struct v4l2_requestbuffers req;
    req.count = buf_num;                    //帧缓冲数量
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; //视频捕获缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;          //内存映射方式
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        printf("VIDIOC_REQBUFS 设置缓冲区失败\n");
        return -1;
    }
 
 
    /*映射内核缓存区到用户空间缓冲区*/
    for(unsigned int i = 0; i < buf_num; ++i)
    {
        /*查询内核缓冲区信息*/
        struct v4l2_buffer v4l2_buf;
        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;
        v4l2_buf.index = i;
        if(ioctl(fd , VIDIOC_QUERYBUF, &v4l2_buf) < 0){
            printf("VIDIOC_QUERYBUF failed\n");
            return -1;
        }
 
        /* 建立映射关系
         * 注意这里的索引号，v4l2_buf.index 与 usr_buf 的索引是一一对应的,
         * 当我们将内核缓冲区出队时，可以通过查询内核缓冲区的索引来获取用户缓冲区的索引号，
         * 进而能够知道应该在第几个用户缓冲区中取数据
         */
        usr_buf[i].length = v4l2_buf.length;
        usr_buf[i].start = (char *)mmap(0, v4l2_buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, v4l2_buf.m.offset);
 
        if (MAP_FAILED == usr_buf[i].start){//若映射失败,打印错误
            printf("mmap failed: %d\n",i);
            return -1;
        }else{
            if (ioctl(fd, VIDIOC_QBUF, &v4l2_buf) < 0){ // 若映射成功则将内核缓冲区入队
                printf("VIDIOC_QBUF failed\n");
                return -1;
            }
        }
    }
    return 0;
}
 
/**
 * @brief stream_on 开启视频流
 * @return 成功返回0，失败返回-1
 */
int stream_on()
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON failed\n");
        return -1;
    }
    return 0;
}
 
/**
 * @brief write_frame 读取一帧图像
 * @return  返回图像帧的索引index,读取失败返回-1
 */
int write_frame()
{
    struct v4l2_buffer v4l2_buf;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_DQBUF, &v4l2_buf) < 0) // 内核缓冲区出队列
    {
        printf("VIDIOC_DQBUF failed, dropped frame\n");
        return -1;
    }
 
 
    /*
     * 因为内核缓冲区与用户缓冲区建立的映射，所以可以通过用户空间缓冲区直接访问这个缓冲区的数据
     */
    char buffer[256];
    sprintf(buffer,"/tmp/pic_%d.mjpg",v4l2_buf.index);
    int file_fd = open(buffer,O_RDWR | O_CREAT); // 若打开失败则不存储该帧图像
    if(file_fd > 0){
        printf("正在保存第%d帧图像\n",v4l2_buf.index);
        write(file_fd,usr_buf[v4l2_buf.index].start,v4l2_buf.bytesused);
        close(file_fd);
    }
 
 
    if (ioctl(fd, VIDIOC_QBUF, &v4l2_buf) < 0) //缓冲区重新入队
    {
        printf("VIDIOC_QBUF failed, dropped frame\n");
        return -1;
    }
    return v4l2_buf.index;
}
 
 
/**
 * @brief stream_off 关闭视频流
 * @return 成功返回0,失败返回-1
 */
int stream_off()
{
    /*关闭视频流*/
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd,VIDIOC_STREAMOFF,&type) == -1)
    {
        printf("Fail to ioctl 'VIDIOC_STREAMOFF'");
        return -1;
    }
    return 0;
}
 
/**
 * @brief unmap_buffer 解除缓冲区映射
 * @return 成功返回0,失败返回-1
 */
int unmap_buffer()
{
    /*解除内核缓冲区到用户缓冲区的映射*/
    for(unsigned int i = 0; i < buf_num; i++)
    {
        int ret = munmap(usr_buf[i].start, usr_buf[i].length);
        if (ret < 0)
        {
            printf("munmap failed\n");
            return -1;
        }
    }
    free(usr_buf); // 释放用户缓冲区内存
    return 0;
}
 
/**
 * @brief release_camera 关闭设备
 */
void release_camera()
{
    close(fd);
}
 
int main(void)
{
    int ret = init_camera("/dev/video0");
    if(ret < 0){
        printf("init_camera error\n");
        return -1;
    }
 
    ret = mmap_buffer();
    if(ret < 0){
        printf("mmap_buffer error\n");
        return -1;
    }
 
    ret = stream_on();
    if(ret < 0){
        printf("stream_on error\n");
        return -1;
    }
 
    for(int i=0;i<100;i++)
    {
        write_frame();
    }
 
    ret = stream_off();
    if(ret < 0){
        printf("stream_off error\n");
        return -1;
    }
 
    ret = unmap_buffer();
    if(ret < 0){
        printf("unmap_buffer error\n");
        return -1;
    }
 
    release_camera();
    return 0;
}
/*)
————————————————
版权声明：本文为CSDN博主「永不做码农」的原创文章，遵循 CC 4.0 BY-SA 版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/a499957739/article/details/89217138 
zzl注：在Ubuntu16.04x64中可以正常测试成功，测试结果是摄像头捕获图像将在/tmp目录下生成名为pic_1.mjpg的图片 */
