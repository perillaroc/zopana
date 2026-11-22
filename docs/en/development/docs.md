# Maintain and deploy the documentation

The documentation uses Markdown. Simplified Chinese pages are under `docs/`, and their English counterparts use the same relative filenames under `docs/en/`. The language menu opens the corresponding page in the other language. Navigation and search settings are in `docs/.vitepress/config.mts`.

Use the player version that matches the documentation when updating instructions. Update the corresponding page in both languages and keep the explicit section IDs used by cross-page links.

## Preview and build locally

Install Node.js 22 and run these PowerShell commands from the repository root.

```powershell
cd docs
npm.cmd ci
npm.cmd run docs:dev
```

Open the local address printed in the terminal. Markdown edits reload in the browser. After stopping the development server, run the following commands in `docs/` to build and preview the production site.

```powershell
npm.cmd run docs:build
npm.cmd run docs:preview
```

The build writes to `docs/.vitepress/dist/`. [VitePress deployment guide](https://vitepress.dev/guide/deploy)

## Deploy to Netlify

Sign in to Netlify and drag `docs/.vitepress/dist/` into the manual deployment area for a new site. For an existing site, drag the rebuilt folder to that site's **Deploys** page. [Netlify manual deployments](https://docs.netlify.com/deploy/create-deploys/#drag-and-drop)

When you connect the Git repository, `netlify.toml` tells Netlify to install dependencies in `docs/`, run `npm run docs:build`, and publish the resulting `.vitepress/dist/` folder. [Netlify build configuration](https://docs.netlify.com/build/configure-builds/file-based-configuration/)
