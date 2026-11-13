# 文档维护与部署

文档用 Markdown 编写。页面位于 `docs/`，导航和中文搜索配置位于 `docs/.vitepress/config.mts`。更新操作说明时，使用与文档对应的播放器版本。

## 本地预览与构建

安装 Node.js 22 后，从仓库根目录执行以下 PowerShell 命令。

```powershell
cd docs
npm.cmd ci
npm.cmd run docs:dev
```

打开终端给出的本地地址，编辑 Markdown 后页面会热更新。停止开发服务器后，在 `docs/` 中构建并预览上线产物。

```powershell
npm.cmd run docs:build
npm.cmd run docs:preview
```

构建生成 `docs/.vitepress/dist/`。[VitePress 部署指南](https://vitepress.dev/guide/deploy)

## 发布到 Netlify

在 Netlify 登录后，可以把 `docs/.vitepress/dist/` 拖到新站点的手动部署区。更新已有站点时，把重新构建的目录拖到该站点的 **Deploys** 页面。[Netlify 手动部署](https://docs.netlify.com/deploy/create-deploys/#drag-and-drop)

连接 Git 仓库后，根目录的 `netlify.toml` 会让 Netlify 在 `docs/` 安装依赖并运行 `npm run docs:build`，然后发布该目录下的 `.vitepress/dist/`。[Netlify 构建配置](https://docs.netlify.com/build/configure-builds/file-based-configuration/)
