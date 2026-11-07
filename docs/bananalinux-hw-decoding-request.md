# Draft: request to BananaLinux for hardware video decoding

*Posted as [JorreFog/BananaLinux#208](https://github.com/JorreFog/BananaLinux/issues/208). It asks for a change to `buildroot/board/rgds/linux-banana.fragment`.*

---

**Title:** Build the RK3568's video decoders as modules (V4L2 stateless: Hantro and rkvdec)

**Body:**

Hi! I'm writing [Banana Player](https://github.com/tonywei92/banana-player), a video and music player for BananaOS. It
uses both screens: the picture on the top, the controls on the bottom touch screen. It decodes in software today, and
the A55 cores keep up with 480p/720p H.264 but fall behind on 1080p and HEVC.

The RK3568 has two hardware decoders that mainline drives through V4L2's stateless API:

- **Hantro G1** (`hantro-vpu`, `rockchip,rk3568-vpu`): H.264, MPEG-2, VP8
- **rkvdec** (`rockchip-vdec`): H.264, HEVC, VP9 (the RK3568's VDPU346; support for it is recent, so it's worth
  checking that 7.2 has it)

ROCKNIX's config (`linux-rocknix.config`) has both (`CONFIG_VIDEO_HANTRO=m`, `CONFIG_VIDEO_HANTRO_ROCKCHIP=y`,
`CONFIG_VIDEO_ROCKCHIP_VDEC=y`). `linux-banana.fragment` turns all of media off (`# CONFIG_MEDIA_SUPPORT is not set`),
which makes sense for a kernel with "nothing on the boot path the menu doesn't need".

Would you take them back **as modules**? Nothing would load at boot, so boot time and the menu stay the same. Only a
player that wants them runs `modprobe`. For example, instead of `# CONFIG_MEDIA_SUPPORT is not set`:

```
CONFIG_MEDIA_SUPPORT=m
CONFIG_MEDIA_SUPPORT_FILTER=y
CONFIG_MEDIA_PLATFORM_SUPPORT=y
# CONFIG_MEDIA_CAMERA_SUPPORT is not set
# CONFIG_MEDIA_ANALOG_TV_SUPPORT is not set
# CONFIG_MEDIA_DIGITAL_TV_SUPPORT is not set
# CONFIG_MEDIA_RADIO_SUPPORT is not set
# CONFIG_MEDIA_SDR_SUPPORT is not set
# CONFIG_MEDIA_TEST_SUPPORT is not set
CONFIG_V4L_MEM2MEM_DRIVERS=y
CONFIG_VIDEO_HANTRO=m
CONFIG_VIDEO_HANTRO_ROCKCHIP=y
CONFIG_VIDEO_ROCKCHIP_VDEC=m
```

(The exact symbols may need adjusting for 7.2.7: rkvdec has moved out of staging in recent kernels, and ROCKNIX's
config already has it as `CONFIG_VIDEO_ROCKCHIP_VDEC`.) The device tree
also needs the `vpu` and `vdec` nodes enabled (`status = "okay"`) for the RG DS and RG DS Plus, if ROCKNIX doesn't
already enable them.

On the player's side, I'd build FFmpeg with the v4l2-request hwaccel (Kwiboo's patches, as LibreELEC and ROCKNIX
use) and decode through mpv's DRM-PRIME path, falling back to software when the modules aren't there. I'm happy to
test on an RG DS Plus.

Thanks for BananaOS!
