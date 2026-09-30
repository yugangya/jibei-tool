# 抽水蓄能电站建设期水土保持智能辅助决策 Demo

本项目以 `Jianghe_tool` 的 Vue 3、TypeScript、Vite、Pinia、Tauri 2 / Rust 技术体系为基础，采用 Web 三维地图，不再采用 C++/QGIS 桌面方案。

当前版本用于验证研究内容 3 的产品闭环：

`三维场景 → 小流域/重点区域 → 措施组合 → 投资初算 → 验收要点 → 两期变化 → 重新决策`

## 可行性结论

方案可行，但应将“展示与业务编排”和“真实遥感/GIS 计算”分开：

- Web 三维地图负责三维场景、3D Tiles、影像、专题区和交互展示。
- Tauri/Rust 负责本机文件访问、任务编排和离线交付。
- GDAL/WhiteboxTools 负责 DEM 填洼、D8 流向、汇流累积、河网和小流域计算。
- 已训练的分割模型负责施工扰动、裸地、边坡和植被对象识别。
- 规则引擎负责水保措施、工程量、投资和验收要点生成。

内置数据可以立即演示完整流程；真实 DEM 和两期无人机影像目前完成了输入、状态和成果接口设计，尚未在缺少生产数据及模型资源时伪造“实算结果”。

## 已实现功能

- 三维地图与二/三维切换、复位、重点区定位。
- 天地图卫星影像、影像注记和在线地形，Key 已在代码中内置：`c44dba5ecf07841f091706edc3e2bba0`。
- 启动时默认请求 `/modes/tileset.json`；加载成功后按模型中心迁移示范图层并定位到抽蓄电站位置。
- 6 个 DEM 小流域示范分区和 5 类典型重点区域：沟道型弃渣场、高陡边坡、施工道路、施工生产生活区、表土堆场。
- 工程、植物、临时措施组合及工程量、综合单价、独立费用、预备费、总投资初算。
- 措施验收核查要点、规则编号、价格版本、置信度和 A/B/C 专业复核门。
- 基准期、复核期和变化分析三种视图，展示恢复面积、扰动扩大、植被覆盖变化和措施保护变化。
- 投资 CSV 和可追溯决策 JSON 结果包导出。
- Tauri 2 桌面壳与 Rust 后端能力探测命令。
- 浏览器 Demo 与真实生产适配两种运行状态。

## 快速启动

环境要求：Node.js 18+。桌面版另需 Rust stable 和 Windows WebView2 开发环境。

```powershell
cd F:\Project_2026\08_Jibei\c_project\water-conservation-demo
npm install
npm run dev
```

浏览器访问终端显示的地址。生产构建：

```powershell
npm run build
```

桌面端开发和校验：

```powershell
npm run tauri:check
npm run tauri:dev
```

当前 `bundle.active` 为 `false`，适合 Demo 阶段快速生成桌面可执行文件而不制作安装包。正式交付时再配置产品图标、签名和安装器。

## 天地图配置

浏览器端 Key 已按项目要求直接配置在 `src/config/app.ts` 中；如需临时切换账号，有两种本机覆盖方式：

1. 打开系统右侧“数据接入”，输入 Key 后保存；Key 只存入当前浏览器/WebView 的 `localStorage`。
2. 将 `.env.example` 复制为 `.env.local`，在 `VITE_TDT_KEY` 中填写新的 Key。`.env.local` 已被 Git 忽略。

建议在天地图控制台设置允许访问的域名白名单。修改环境变量后需要重新启动开发服务。

## 3D Tiles 模型

把模型完整复制到：

```text
public/modes/
├─ tileset.json
├─ *.b3dm / *.glb / *.json
└─ 纹理及子目录
```

应用启动时自动检查 `public/modes/tileset.json`。当前项目只创建了接入目录和说明文件，没有在参考工程或当前工作区发现实际模型数据，因此模型文件需另行复制；缺失时系统仍可运行三维业务图层 Demo。

如入口不同，可在 `.env.local` 修改：

```dotenv
VITE_TILESET_URL=/modes/tileset.json
```

## 两期无人机影像

三维前端接收已经发布的 XYZ/TMS 影像服务：

```dotenv
VITE_PERIOD1_IMAGERY_URL=/imagery/baseline/{z}/{x}/{y}.png
VITE_PERIOD2_IMAGERY_URL=/imagery/current/{z}/{x}/{y}.png
```

原始无人机照片不能直接用于可信变化检测。生产处理顺序应为：

1. 空三解算，生成两期 DOM/DSM；
2. 统一坐标系、分辨率和范围；
3. 同名点精配准，控制平面残差；
4. 辐射一致化，削弱季节、光照和阴影差异；
5. 扰动/裸地/植被/边坡语义分割；
6. 对象级叠加分析，区分恢复、扩大、转移和误检；
7. 人工复核后触发规则和投资重新计算。

## 真实 DEM 小流域接口

建议输入：

- 投影坐标系、米制单位的 DEM GeoTIFF；
- 工程红线与施工布置边界；
- 可选沟口/控制点；
- 河网阈值、最小汇水面积和最小斑块面积。

生产后端输出：

- `filled_dem.tif`
- `d8_pointer.tif`
- `flow_accumulation.tif`
- `stream_network.geojson`
- `watersheds.geojson`
- `slope_risk.geojson`
- `analysis_manifest.json`

前端读取 GeoJSON 属性时，至少需要小流域编号、面积、出口高程、径流系数、侵蚀风险和河网等级。坡度、施工边界和影像识别结果叠加后形成重点区域，再进入规则决策。

## 代码结构

```text
src/
├─ components/          # 三维地图、图层、时序、决策、数据接入
├─ config/              # 环境配置和版本号
├─ data/                # 内置空间示范数据
├─ services/            # 规则、估算、变化、导出、Tauri 桥接
├─ stores/              # Pinia 项目状态
├─ styles/              # 深色 GIS 驾驶舱主题
└─ types/               # 领域数据模型
src-tauri/
├─ src/main.rs          # Tauri 后端与生产能力边界接口
├─ capabilities/        # 最小权限配置
├─ Cargo.toml
└─ tauri.conf.json
public/modes/            # 默认 3D Tiles 目录
```

## Demo 与生产版边界

- 当前规则和价格为演示版本，不替代正式水保方案、施工图设计、概估算编制和法定审查。
- 高风险弃渣场、高陡边坡等结果强制进入 C 级复核，必须补充地勘、水文、稳定及结构计算。
- 真实识别精度必须通过样本、混淆矩阵、现场核查和对象级误差评估证明。
- 水保投资正式版需接入项目所在地价格基期、材料价、人工调整系数和费率。

## 下一步生产化顺序

1. 提供真实 DEM、工程红线、施工布置和 `modes` 模型。
2. 选定 GDAL/WhiteboxTools 运行包，完成 Rust 任务封装和进度事件。
3. 提供两期正射影像与少量人工标注样本，验证已有识别模型或再训练。
4. 将专家确认的规则、出处条文、综合单价和验收指标导入版本化数据库。
5. 用一个在建电站的 3～5 个典型扰动单元做盲评和参数校准。
