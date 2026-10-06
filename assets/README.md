# Assets

本目录用于存放渲染示例资产。

## Bistro 数据集

间接绘制示例使用 Amazon Lumberyard Bistro 的室外场景，BC7 纹理压缩示例使用其中一张纹理。这个数据集体积较大，不提交到 Git。

可以从下面地址下载 Bistro 数据集。解压后将五个目录放到 `assets/bistro/`，最终结构应为：

```text
assets/bistro/
├── BuildingTextures/
├── Exterior/
│   ├── exterior.obj
│   └── exterior.mtl
├── Interior/
├── OtherTextures/
└── PropTextures/
```

https://casual-effects.com/data/

## Rubber Duck

`rubber_duck/` 是一个较小的 glTF 模型资产，用于 Buffer、模型加载和多 Pipeline 示例。这个目录已提交到仓库，通过 Git LFS 管理。

## Piazza Bologni HDR

`piazza_bologni_1k.hdr` 用于 cube map 环境反射示例，同样通过 Git LFS 管理。
