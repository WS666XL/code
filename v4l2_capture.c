/*
 * v4l2_capture.c —— V4L2 摄像头采集最小 demo（MMAP + select）
 *
 * 编译：gcc -O2 -Wall -Wextra v4l2_capture.c -o v4l2_capture
 * 运行：./v4l2_capture [/dev/videoX] [帧数] [输出前缀]
 *       默认：/dev/video0、10 帧、前缀 frame
 *
 * 保存格式：YUYV -> .ppm（转成 RGB，可直接用图片查看器打开）
 *           MJPEG/JPEG -> .jpg（原始 JPEG 数据）
 *           其它格式 -> .bin（原始帧数据）
 *
 * ============================================================================
 * 一、标准 V4L2 采集流程（本文件就是按这个顺序写的）
 * ============================================================================
 *
 *   1) open("/dev/video0")                 打开设备
 *   2) VIDIOC_QUERYCAP                     确认它支持 capture + streaming
 *   3) VIDIOC_ENUM_FMT / VIDIOC_S_FMT      看支持哪些格式，设置分辨率/像素格式
 *   4) VIDIOC_REQBUFS + QUERYBUF + mmap    申请内核缓冲区并映射到用户态
 *   5) VIDIOC_QBUF / VIDIOC_STREAMON       空缓冲区全部入队，启动视频流
 *   6) select() 等可读 -> VIDIOC_DQBUF     取出一帧 -> 保存 -> QBUF 还回
 *   7) VIDIOC_STREAMOFF -> munmap -> close 停止并释放
 *
 * ============================================================================
 * 二、缓冲区是怎么"转"起来的
 * ============================================================================
 *
 *   REQBUFS  驱动在内核里分配 N 个缓冲区
 *   QUERYBUF 问清楚每个缓冲区的大小 length 和偏移 m.offset
 *   mmap     把内核缓冲区映射进本进程；之后直接读写内存，不需要拷贝
 *
 *   QBUF     用户把"空缓冲区"交还给驱动         [用户] --QBUF--> [驱动空闲队列]
 *   填帧     驱动/硬件把一帧数据写进缓冲区        [驱动空闲队列] --写满--> [完成队列]
 *   DQBUF    用户从完成队列取走"满缓冲区"         [完成队列] --DQBUF--> [用户]
 *   保存     用户读取 buffers[buf.index].start
 *   QBUF     立刻把缓冲区再还回去，继续下一轮
 *
 *   关键点：DQBUF 之后这块缓冲区归用户所有，驱动不会再动它；如果用完不 QBUF，
 *          驱动可用的空闲缓冲区会越来越少，最后直接停止采集（画面卡住）。
 *
 * ============================================================================
 * 三、几个容易踩的坑
 * ============================================================================
 *
 *   - S_FMT 是"请求"，驱动可能改分辨率、换像素格式、做行对齐；
 *     所以调用后必须读回 fmt 里的实际值，后面的逻辑都以读回值为准。
 *   - bytesperline 可能大于 width*2（行末有对齐填充），按行处理时要用它当 stride，
 *     不能直接假设"一行紧挨着一行"。
 *   - bytesused 是这一帧的有效字节数，buf.length 是缓冲区总大小，二者不是一回事。
 *   - /dev/video0 不一定是采集节点。UVC 摄像头经常同时暴露多个 /dev/videoN
 *     （采集、metadata 等），QUERYCAP 不通过就换一个节点试。
 *   - 打开时加 O_NONBLOCK，配合 select() 使用，避免 DQBUF 永久阻塞。
 *
 * 说明：本 demo 只支持 single-planar（V4L2_CAP_VIDEO_CAPTURE），
 *       USB 摄像头基本都是这种；部分 SoC ISP 的 mplane 接口不在范围内。
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

/* 把结构体所有字段清零。V4L2 的 ioctl 很多都要求"未用字段必须为 0"，
 * 否则老驱动可能因为脏字段返回 EINVAL。 */
#define CLEAR(x) memset(&(x), 0, sizeof(x))

/* mmap 之后每个缓冲区的描述：起始地址 + 长度，munmap 时要用 */
struct buffer {
    void   *start;
    size_t  length;
};

/* 驱动分配的缓冲区数组，下标就是 DQBUF/QBUF 里的 buf.index */
static struct buffer *buffers;
static unsigned int   n_buffers;

/* 打开的 /dev/videoN 文件描述符，-1 表示还没打开 */
static int            fd = -1;

/* 是否已经 STREAMON，决定退出时要不要 STREAMOFF */
static int            streaming;

/* 驱动确认后的实际参数（S_FMT 之后可能和请求值不同） */
static int          width  = 640;
static int          height = 480;
static unsigned int pixelformat;
static unsigned int bytesperline;

/*
 * ioctl 的封装。
 * ioctl 可能被信号（Ctrl+C、定时器等）打断并返回 EINTR，这不是真正的错误，
 * 标准做法是继续重试，而不是直接失败。
 */
static int xioctl(int fh, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && errno == EINTR);   /* 被信号打断时重试 */
    return r;
}

/*
 * fourcc 是"4 个字符打包进一个 32 位整数"的格式标识，比如 'YUYV'。
 * 这里把整数按字节拆回可读字符串，只用于打印。
 */
static void fourcc_to_str(unsigned int fourcc, char out[8])
{
    snprintf(out, 8, "%c%c%c%c",
             (fourcc >> 0) & 0xff, (fourcc >> 8) & 0xff,
             (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);
}

/*
 * 打开设备节点。
 *   O_RDWR    : 采集一般要求可读写
 *   O_NONBLOCK: 非阻塞模式。后面用 select() 等帧，DQBUF 就不会永久卡住；
 *               驱动暂时没帧时，DQBUF 会返回 EAGAIN 而不是睡眠。
 */
static int open_device(const char *dev)
{
    fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "打开 %s 失败：%s\n", dev, strerror(errno));
        /* 两个新手最常遇到的错误，直接把排查方向打出来 */
        if (errno == ENOENT)
            fprintf(stderr, "提示：该环境没有摄像头设备节点。"
                            "请连接摄像头，或在虚拟机中把 USB 摄像头直通给 guest。\n");
        else if (errno == EACCES)
            fprintf(stderr, "提示：权限不足，可执行 sudo usermod -aG video $USER 后重新登录。\n");
        return -1;
    }
    return 0;
}

/*
 * 查询并校验设备能力。
 *
 * v4l2_capability 里：
 *   driver / card / bus_info  驱动名、设备名、总线信息，调试时先看这三行
 *   capabilities              整个设备的能力
 *   device_caps               单个节点的能力（新版驱动才有意义）
 *
 * 本 demo 需要的两个能力：
 *   V4L2_CAP_VIDEO_CAPTURE    支持视频采集
 *   V4L2_CAP_STREAMING        支持 streaming I/O（本 demo 用其中的 mmap 方式）
 */
static int print_device_info(void)
{
    struct v4l2_capability cap;
    unsigned int caps;

    CLEAR(cap);
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "VIDIOC_QUERYCAP 失败：%s\n", strerror(errno));
        return -1;
    }

    printf("设备信息：\n");
    printf("  驱动 : %s\n", (const char *)cap.driver);
    printf("  名称 : %s\n", (const char *)cap.card);
    printf("  总线 : %s\n", (const char *)cap.bus_info);

    /* 新驱动会把"整个 USB 设备的能力"和"当前这个节点的能力"分开：
     * 有 V4L2_CAP_DEVICE_CAPS 标志时，必须看 device_caps；
     * 老驱动没有这个概念，只能看 capabilities。 */
    caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                     : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "该节点不支持视频采集（可能是个 metadata 节点，换个 /dev/videoN 试试）\n");
        return -1;
    }
    if (!(caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "该设备不支持 streaming I/O（mmap）\n");
        return -1;
    }
    return 0;
}

/*
 * 枚举设备支持的像素格式，把 fourcc 填入 list，返回个数。
 *
 * 用法是"填好 type，然后 index 从 0 开始递增调用"，直到返回失败（通常是 EINVAL）。
 * 每个 index 对应一种格式，desc.pixelformat 就是该格式的 fourcc。
 *
 * 注意：迭代过程中不要 memset 整个 desc，否则 index 被清零会死循环；
 *       只递增 index 即可。
 */
static int enumerate_formats(unsigned int *list, int max)
{
    struct v4l2_fmtdesc desc;
    int n = 0;

    CLEAR(desc);
    desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("支持的像素格式：\n");
    while (n < max && xioctl(fd, VIDIOC_ENUM_FMT, &desc) == 0) {
        char s[8];
        fourcc_to_str(desc.pixelformat, s);
        printf("  [%u] %-4s  %s%s\n", desc.index, s, desc.description,
               (desc.flags & V4L2_FMT_FLAG_COMPRESSED) ? "（压缩）" : "");
        list[n++] = desc.pixelformat;
        desc.index++;          /* 只递增 index，见上面的说明 */
    }

    if (n == 0) {
        fprintf(stderr, "VIDIOC_ENUM_FMT 没有返回任何格式\n");
        return -1;
    }
    return n;
}

/*
 * 协商采集格式。
 *
 * 先枚举设备支持什么，再按偏好挑：
 *   YUYV  -> 未压缩，方便转 RGB 看效果
 *   MJPEG -> UVC 摄像头最常见，数据量小
 *   RGB24 -> 少数设备能直接给 RGB
 * 一个都不支持，就退而求其次用枚举到的第一个。
 *
 * v4l2_format.fmt.pix 关键字段：
 *   width / height   期望分辨率（驱动可能改成最接近的支持值）
 *   pixelformat      像素格式 fourcc
 *   field            场序；逐行扫描的摄像头用 ANY/NONE 即可
 *   bytesperline     【驱动返回】每行字节数，可能包含行末对齐填充
 *   sizeimage        【驱动返回】一帧图像占用的总字节数
 */
static int set_format(void)
{
    unsigned int supported[64];
    static const unsigned int prefer[] = {
        V4L2_PIX_FMT_YUYV,      /* 未压缩，方便转 RGB 查看 */
        V4L2_PIX_FMT_MJPEG,     /* UVC 摄像头最常见的压缩格式 */
        V4L2_PIX_FMT_RGB24,
    };
    unsigned int chosen = 0;
    struct v4l2_format fmt;
    char s[8];
    int n, i;
    size_t p;

    n = enumerate_formats(supported, 64);
    if (n < 0)
        return -1;

    /* 两层循环：外层按偏好顺序，内层在设备支持列表里找；
     * 找到第一个就算选定。 */
    for (p = 0; p < sizeof(prefer) / sizeof(prefer[0]) && !chosen; p++) {
        for (i = 0; i < n; i++) {
            if (supported[i] == prefer[p]) {
                chosen = prefer[p];
                break;
            }
        }
    }
    if (!chosen)
        chosen = supported[0];

    CLEAR(fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = chosen;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;  /* 让驱动自己决定场序 */

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "VIDIOC_S_FMT 失败：%s\n", strerror(errno));
        return -1;
    }

    /* S_FMT 只是"请求"，驱动可能：
     *   - 把分辨率改成最接近的支持值
     *   - 把像素格式换成它实际能给的
     *   - 设置 bytesperline 做行对齐
     * 所以这里必须读回实际值，并覆盖全局变量。 */
    width        = fmt.fmt.pix.width;
    height       = fmt.fmt.pix.height;
    pixelformat  = fmt.fmt.pix.pixelformat;
    bytesperline = fmt.fmt.pix.bytesperline;

    fourcc_to_str(pixelformat, s);
    printf("实际格式：%s %dx%d, bytesperline=%u, sizeimage=%u\n",
           s, width, height, bytesperline, fmt.fmt.pix.sizeimage);
    return 0;
}

/*
 * 申请并映射缓冲区（MMAP 方式）。
 *
 * 1. REQBUFS：告诉驱动"我要 count 个 MMAP 缓冲区"。
 *    驱动可能少给（比如内存紧张），所以之后一律用 req.count，不用请求值。
 * 2. 对每个 index 调 QUERYBUF：拿到 length（缓冲区大小）和 m.offset
 *    （它在设备内存里的偏移），mmap 必须用这两个值。
 * 3. mmap：把内核缓冲区映射进本进程地址空间。之后直接读写
 *    buffers[i].start 就能拿到帧数据，不需要 read() 那种整帧拷贝。
 *
 * 为什么用 mmap 而不是 read()：
 *   read() 每一帧都要把数据从内核复制到用户态；分辨率/帧率一高，拷贝开销明显。
 *   mmap 是共享映射，驱动写完用户直接就能看到，省掉这次拷贝。
 */
static int init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.count  = 4;                          /* 4 个缓冲区，轮转起来更不容易丢帧 */
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;           /* 申请 mmap 型缓冲区 */

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        if (errno == EINVAL)
            fprintf(stderr, "设备不支持 mmap 方式的 streaming I/O\n");
        else
            fprintf(stderr, "VIDIOC_REQBUFS 失败：%s\n", strerror(errno));
        return -1;
    }
    if (req.count < 2) {
        fprintf(stderr, "可用缓冲区不足（%u 个）\n", req.count);
        return -1;
    }

    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        perror("calloc");
        return -1;
    }

    for (n_buffers = 0; n_buffers < req.count; n_buffers++) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = n_buffers;              /* 问第 n_buffers 个缓冲区的信息 */

        /* QUERYBUF 填回：buf.length（大小）、buf.m.offset（设备内存偏移） */
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QUERYBUF 失败：%s\n", strerror(errno));
            return -1;
        }

        buffers[n_buffers].length = buf.length;

        /* 把 fd 对应的设备内存中 [offset, offset+length) 这段映射到进程里。
         * MAP_SHARED：驱动和用户看到的是同一块内存，驱动写完后用户能直接读。 */
        buffers[n_buffers].start  = mmap(NULL, buf.length,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, fd, buf.m.offset);
        if (buffers[n_buffers].start == MAP_FAILED) {
            fprintf(stderr, "mmap 失败：%s\n", strerror(errno));
            buffers[n_buffers].start = NULL; /* 置 NULL，退出时 munmap 会跳过它 */
            return -1;
        }
    }
    return 0;
}

/*
 * 启动视频流。
 *
 * 第一步：把所有空缓冲区 QBUF 入队。
 *   驱动必须先从队列里拿到缓冲区，才有地方写数据；一个都不给，它没法采。
 *
 * 第二步：STREAMON。通知驱动"开始出流"：
 *   UVC 摄像头会开始传输数据，驱动随即把填满的缓冲区放进完成队列。
 *
 * 注意顺序：QBUF 必须在 STREAMON 之前（至少要有可用的缓冲区）。
 */
static int start_capturing(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    unsigned int i;

    /* 所有缓冲区先入队，驱动才有地方写数据 */
    for (i = 0; i < n_buffers; i++) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;                      /* 把第 i 个缓冲区交出去 */

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF 失败：%s\n", strerror(errno));
            return -1;
        }
    }

    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "VIDIOC_STREAMON 失败：%s\n", strerror(errno));
        return -1;
    }
    streaming = 1;
    return 0;
}

/*
 * 停止视频流。
 * STREAMOFF 之后驱动不再往缓冲区写数据，所有缓冲区回到用户手里。
 * 停止失败也不影响程序退出，所以忽略返回值。
 */
static void stop_capturing(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (streaming) {
        xioctl(fd, VIDIOC_STREAMOFF, &type);
        streaming = 0;
    }
}

/* 把 RGB 分量限制在 0~255，防止 YUV 转换后溢出 */
static inline int clamp255(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/*
 * YUYV422（也叫 YUY2）转 RGB24。
 *
 * 内存排列：每 4 字节描述 2 个像素，两个像素共用一组 U/V：
 *
 *   字节:   [0]   [1]   [2]   [3]
 *           Y0    U0    Y1    V0
 *   像素:   P0          P1
 *   含义:   亮度0 色度U 亮度1 色度V
 *
 * YUV -> RGB（简化 BT.601）：
 *   R = Y + 1.402*(V-128)
 *   G = Y - 0.344*(U-128) - 0.714*(V-128)
 *   B = Y + 1.772*(U-128)
 *
 * src_stride 是源数据每行的字节数，可能是 bytesperline（含对齐填充），
 * 不一定等于 w*2；dst 是紧凑排列的 RGB24，每行 w*3 字节。
 */
static void yuyv_to_rgb(const unsigned char *src, unsigned int src_stride,
                        unsigned char *dst, int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++) {
        /* 源数据按行寻址：跳过每行末尾可能的填充字节 */
        const unsigned char *row = src + (size_t)y * src_stride;
        /* 输出是紧凑的：每行 w*3 字节 */
        unsigned char *out = dst + (size_t)y * (size_t)w * 3;

        /* 一次处理两个像素（4 字节 YUYV），所以 x 每次加 2 */
        for (x = 0; x + 1 < w; x += 2) {
            int y0 = row[0];                 /* 像素 0 的亮度 */
            int u  = row[1] - 128;           /* 两个像素共用的 U */
            int y1 = row[2];                 /* 像素 1 的亮度 */
            int v  = row[3] - 128;           /* 两个像素共用的 V */
            /* 简化的 BT.601 YUV -> RGB */
            int r0 = clamp255(y0 + (int)(1.402 * v));
            int g0 = clamp255(y0 - (int)(0.344 * u) - (int)(0.714 * v));
            int b0 = clamp255(y0 + (int)(1.772 * u));
            int r1 = clamp255(y1 + (int)(1.402 * v));
            int g1 = clamp255(y1 - (int)(0.344 * u) - (int)(0.714 * v));
            int b1 = clamp255(y1 + (int)(1.772 * u));

            /* 紧凑写入两个像素：R G B R G B */
            out[0] = (unsigned char)r0;
            out[1] = (unsigned char)g0;
            out[2] = (unsigned char)b0;
            out[3] = (unsigned char)r1;
            out[4] = (unsigned char)g1;
            out[5] = (unsigned char)b1;

            row += 4;                        /* 源：前进 4 字节 */
            out += 6;                        /* 目标：前进 2 像素 = 6 字节 */
        }
    }
}

/*
 * 写 PPM(P6) 文件——最简单的无损 RGB 图片格式，不需要任何图像库。
 *
 * 文件内容：
 *   "P6\n" + "宽 高\n" + "255\n" + 紧密排列的 RGB 字节
 *
 * stride 用于处理源数据的行对齐：逐行只写有效的前 w*3 字节，
 * 跳过每行末尾的填充。
 */
static int write_ppm(const char *path, const void *data, int w, int h,
                     unsigned int stride)
{
    FILE *f = fopen(path, "wb");
    const unsigned char *p = data;
    int y;

    if (!f) {
        perror(path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);

    if (stride == (unsigned int)w * 3) {
        /* 源数据本来就是紧凑的，一次写完 */
        fwrite(p, 1, (size_t)w * (size_t)h * 3, f);
    } else {
        /* 有行填充：一行一行写，只取每行前 w*3 字节 */
        for (y = 0; y < h; y++)
            fwrite(p + (size_t)y * stride, 1, (size_t)w * 3, f);
    }
    fclose(f);
    return 0;
}

/*
 * 按像素格式保存一帧：
 *   MJPEG/JPEG -> .jpg（摄像头输出的就是完整 JPEG 码流，直接落盘即可）
 *   YUYV       -> .ppm（先转 RGB24，再写 PPM）
 *   RGB24      -> .ppm（注意源数据可能有行填充，用 stride 处理）
 *   其它格式   -> .bin（原样保存，之后可以用 ffmpeg 等工具再处理）
 *
 * 参数 size 是驱动给的 bytesused（本帧有效字节数），不是缓冲区总长度 length。
 */
static int save_frame(const void *data, size_t size, int index,
                      const char *prefix)
{
    char path[256];
    FILE *f;

    if (pixelformat == V4L2_PIX_FMT_MJPEG || pixelformat == V4L2_PIX_FMT_JPEG) {
        snprintf(path, sizeof(path), "%s_%03d.jpg", prefix, index);
        f = fopen(path, "wb");
        if (!f) {
            perror(path);
            return -1;
        }
        fwrite(data, 1, size, f);
        fclose(f);
        printf("保存 %s（%zu 字节）\n", path, size);
        return 0;
    }

    if (pixelformat == V4L2_PIX_FMT_YUYV) {
        /* YUYV 一行原始数据是 bytesperline 字节；驱动没填就按 w*2 估算 */
        unsigned int stride = bytesperline ? bytesperline : (unsigned int)width * 2;
        size_t rgb_size = (size_t)width * (size_t)height * 3;
        unsigned char *rgb = malloc(rgb_size);

        if (!rgb) {
            perror("malloc");
            return -1;
        }
        yuyv_to_rgb(data, stride, rgb, width, height);
        snprintf(path, sizeof(path), "%s_%03d.ppm", prefix, index);
        if (write_ppm(path, rgb, width, height, (unsigned int)width * 3) < 0) {
            free(rgb);
            return -1;
        }
        free(rgb);
        printf("保存 %s（YUYV 转 RGB，%dx%d）\n", path, width, height);
        return 0;
    }

    if (pixelformat == V4L2_PIX_FMT_RGB24) {
        /* RGB24 已经是目标格式，直接把源数据按行写进 PPM */
        unsigned int stride = bytesperline ? bytesperline : (unsigned int)width * 3;
        snprintf(path, sizeof(path), "%s_%03d.ppm", prefix, index);
        if (write_ppm(path, data, width, height, stride) < 0)
            return -1;
        printf("保存 %s（RGB24，%dx%d）\n", path, width, height);
        return 0;
    }

    /* 其它格式（NV12、GREY 等）先原样落盘，方便用 ffmpeg 等工具再处理 */
    snprintf(path, sizeof(path), "%s_%03d.bin", prefix, index);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    printf("保存 %s（原始数据，%zu 字节）\n", path, size);
    return 0;
}

/*
 * 采集主循环：select -> DQBUF -> 保存 -> QBUF
 *
 * select(fd) 的作用：
 *   阻塞等待 fd 变成"可读"。对正在出流的 V4L2 设备来说，可读意味着
 *   完成队列里至少有一帧可取。这里加了 2 秒超时，防止设备异常时永远卡死。
 *
 * DQBUF（DeQueue Buffer）：从完成队列取走一个已填满的缓冲区。
 *   - buf.index     告诉你取到的是哪个缓冲区 -> buffers[buf.index]
 *   - buf.bytesused 本帧有效数据长度
 *   - buf.flags     可能带 V4L2_BUF_FLAG_ERROR，表示帧数据损坏
 *
 * QBUF（Queue Buffer）：把刚才的缓冲区立刻还回空闲队列。
 *   不还回去，驱动手里可用的缓冲区会耗尽，采集就会停住。
 */
static int capture(int frame_count, const char *prefix)
{
    unsigned int got = 0;

    while (got < (unsigned int)frame_count) {
        fd_set fds;
        struct timeval tv;
        struct v4l2_buffer buf;
        int r;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);                    /* 监听这个设备 fd */
        tv.tv_sec  = 2;                      /* 最多等 2 秒 */
        tv.tv_usec = 0;

        /* 等驱动把某一帧写满（超过 2 秒没帧就认为出问题了） */
        r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR)
                continue;                    /* 被信号打断，重新等 */
            perror("select");
            return -1;
        }
        if (r == 0) {
            fprintf(stderr, "select 超时：2 秒内没有新帧，停止\n");
            return -1;
        }

        CLEAR(buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        /* 出队：拿到一个已经填好数据的缓冲区 */
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN)
                continue;                    /* 非阻塞模式下暂时没帧，再等 */
            fprintf(stderr, "VIDIOC_DQBUF 失败：%s\n", strerror(errno));
            return -1;
        }
        if (buf.index >= n_buffers) {
            fprintf(stderr, "DQBUF 返回非法 index=%u\n", buf.index);
            return -1;
        }

        if (buf.flags & V4L2_BUF_FLAG_ERROR) {
            /* USB 丢包等导致这一帧不完整，直接丢弃，不计数 */
            fprintf(stderr, "第 %u 帧数据损坏，丢弃\n", got);
        } else {
            /* 取到数据：buffers[buf.index].start 就是帧的首地址 */
            if (save_frame(buffers[buf.index].start, buf.bytesused,
                           (int)got, prefix) < 0)
                return -1;
            got++;
        }

        /* 用完必须还回队列，否则驱动很快就没缓冲区可写 */
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF 失败：%s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

/*
 * 释放资源，顺序和申请时相反：
 *   STREAMOFF（调用方负责） -> munmap -> free -> close
 * 逐个 munmap 时跳过没映射成功的项（start 为 NULL）。
 */
static void uninit_device(void)
{
    unsigned int i;

    for (i = 0; i < n_buffers; i++) {
        if (buffers[i].start)
            munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    buffers   = NULL;
    n_buffers = 0;

    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

/*
 * 程序入口。
 *   argv[1] 设备节点，默认 /dev/video0
 *   argv[2] 采集帧数，默认 10
 *   argv[3] 输出文件前缀，默认 frame（生成 frame_000.ppm 等）
 *
 * 主流程就是文件头列出的七步；任何一步失败都走 fail 标签统一清理。
 */
int main(int argc, char **argv)
{
    const char *dev     = (argc > 1) ? argv[1] : "/dev/video0";
    int         frames  = (argc > 2) ? atoi(argv[2]) : 10;
    const char *prefix  = (argc > 3) ? argv[3] : "frame";
    int         rc;

    if (frames <= 0) {
        fprintf(stderr, "用法：%s [/dev/videoX] [帧数>0] [输出前缀]\n", argv[0]);
        return 1;
    }

    printf("打开 %s ...\n", dev);
    fflush(stdout);                          /* 让提示先于错误信息出现 */

    /* 1) 打开设备 */
    if (open_device(dev) < 0)
        goto fail;
    /* 2) 查询能力：不支持采集/streaming 就没必要继续 */
    if (print_device_info() < 0)
        goto fail;
    /* 3) 枚举格式并设置分辨率/像素格式（以驱动返回值为准） */
    if (set_format() < 0)
        goto fail;
    /* 4) 申请缓冲区 + mmap 映射 */
    if (init_mmap() < 0)
        goto fail;
    /* 5) 缓冲区入队 + STREAMON */
    if (start_capturing() < 0)
        goto fail;

    /* 6) 循环取帧并保存 */
    printf("开始采集 %d 帧，输出前缀 \"%s\" ...\n", frames, prefix);
    rc = capture(frames, prefix);

    /* 7) 停止、解映射、关闭 */
    stop_capturing();
    uninit_device();

    if (rc == 0) {
        printf("采集完成，共 %d 帧。\n", frames);
        return 0;
    }
    return 1;

fail:
    /* 中途失败：能停就停，然后把已申请的资源全部释放 */
    stop_capturing();
    uninit_device();
    return 1;
}
