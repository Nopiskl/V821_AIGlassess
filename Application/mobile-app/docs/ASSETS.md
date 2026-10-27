# 界面素材

[`glasses-product.png`](../src/static/glasses-product.png) 为生成式智能眼镜产品示意图，
用于首页设备卡片、待连接、配对和设备详情。该素材不作为硬件照片或尺寸依据。

图像尺寸为 1774 × 887，格式为 PNG RGBA，包含透明通道。页面使用 `aspectFit` 等比显示。
场景文件和线性图标采用 SVG。

## 生成记录

生成工具为 `image_gen.imagegen`，使用文本提示生成新图，参数 `transparent_background` 为 `true`。

完整提示词：

```text
Use case: product-mockup. Asset type: transparent isolated hero illustration for a Chinese V821 smart-glasses mobile companion app. Generate a brand-new premium studio product rendering of a single pair of compact black smart eyeglasses, not a UI screenshot. Exactly front-on, centered, level horizontal bridge, symmetrical equal-size rounded rectangular lenses with a restrained blue-gray optical tint, coherent physically plausible hinges and two straight temples receding evenly backward into depth and partly hidden behind the front frame. Natural wearable proportions, slim substantial matte graphite frame, one small circular camera module at the outer front corner. No exaggerated sharp angular arms, no warped surfaces, no crooked bridge, no perspective mismatch, no duplicate glasses. Soft silver edge lighting and subtle cool reflections so the silhouette is visible on both a deep navy card and a pale gray page. Clean realistic product design, restrained highlights, no neon. Wide landscape composition around 2:1, the entire object fully inside the frame with 10 percent transparent margin on all sides, object fills approximately 80 percent of width. True transparent background, no floor, no drop shadow backdrop, no text, no logo, no watermark. This is a visual concept, not a claimed photograph of existing hardware.
```
