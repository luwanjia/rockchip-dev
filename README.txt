
1. 目录说明
    firmware         // 存放开发板升级固件及Linux内核文件
    initrd           // RamDisk初始化代码
    linux-kernel-4.4 // 内核源码及编译教程
    linux-rootfs     // 根文件系统制作教程及SDL集成教程，里面包含一些必须的文件
    rock-tool        // 这是升级包制作工具，制作教程参照rockchip/linux-rootfs/05_制作升级包镜像.txt
    tools            // 这里面就是烧录开发板的工具，主要是upgrade_tool，用法参照tools/Linux_Upgrade_Tool_v1.2/linux_upgrade_tool_v1.16_help.pdf

2. 刷机
    如果只是刷机，只需要用做好的固件（在firmware目录下），按照linux-rootfs/06_烧录镜像到开发板.txt里面的步骤操作即可。

3. 完全从零制作固件
    这涉及到的内容就比较多，包括编译内核和做成根文件系统，以及集成驱动、硬件解码、SDL集成等等。
    内核编译请参照：rockchip/linux-kernel-4.4/01_内核编译.txt，或者直接使用firmware/linux-boot里面的即可，linux-boot_lvds.img支持lcd屏，linux-boot_vga.img支持vga，二选其一。
    根文件系统的做成请参照linux-rootfs目录下面的01到07步骤，依次按照教程操作即可。如下：
        《01_制作纯净ubuntu16.04根文件系统.txt》
        《02_增加驱动及硬件解码支持.txt》
        《03_配置开发板开机自动登录.txt》
        《04_SDL集成到根文件系统.txt》 // 此部分可根据需要决定是否执行
        《05_制作升级包镜像并烧录.txt》
        《06_烧录镜像到开发板.txt》
        《07_单独烧录Linux内核.txt》

4. 关于SDL安装说明
    SDL可以集成到系统固件里面，让开机自动启动，制作根文件系统时，请执行《04_SDL集成到根文件系统.txt》教程；开发板系统启动后，如果需要更新SDL，请将SDL的可执行文件覆盖到/opt/sdl_bin目录下。
    当然也可以跟固件分开，单独安装，制作根文件系统时，请略过《04_SDL集成到根文件系统.txt》，然后通过U盘或者ssh，将SDL的可执行文件复制到开发板，以root权限运行即可。

