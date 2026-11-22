import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'zopana',
  cleanUrls: true,
  vite: { publicDir: '../resources/brand' },
  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: '/zopana.svg' }]],
  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      description: 'zopana Windows 视频播放器的使用手册和开发指南',
      themeConfig: {
        nav: [
          { text: '快速开始', link: '/quick-start' },
          { text: '使用手册', link: '/guide/playback', activeMatch: '^/guide/' },
          { text: '开发指南', link: '/development/build', activeMatch: '^/development/' },
        ],
        sidebar: [
          { text: '快速开始', link: '/quick-start' },
          {
            text: '使用手册',
            items: [
              { text: '播放与窗口', link: '/guide/playback' },
              { text: '缩放拖动与鼠标模式', link: '/guide/zoom-and-pan' },
              { text: '文件与播放列表', link: '/guide/playlist' },
              { text: '音轨、字幕与进阶播放', link: '/guide/media' },
              { text: '截图与设置', link: '/guide/settings' },
            ],
          },
          {
            text: '开发指南',
            items: [
              { text: '构建与启动', link: '/development/build' },
              { text: '便携包与发布验收', link: '/development/packaging' },
              { text: '源码与配置', link: '/development/reference' },
              { text: '文档维护与部署', link: '/development/docs' },
            ],
          },
        ],
        langMenuLabel: '切换语言',
        outline: { label: '本页目录' },
        docFooter: { prev: '上一页', next: '下一页' },
        sidebarMenuLabel: '文档目录',
        returnToTopLabel: '返回顶部',
        darkModeSwitchLabel: '外观',
      },
    },
    en: {
      label: 'English',
      lang: 'en-US',
      description: 'User and developer guides for the zopana Windows video player',
      themeConfig: {
        nav: [
          { text: 'Quick start', link: '/en/quick-start' },
          { text: 'User guide', link: '/en/guide/playback', activeMatch: '^/en/guide/' },
          { text: 'Development', link: '/en/development/build', activeMatch: '^/en/development/' },
        ],
        sidebar: [
          { text: 'Quick start', link: '/en/quick-start' },
          {
            text: 'User guide',
            items: [
              { text: 'Playback and windows', link: '/en/guide/playback' },
              { text: 'Zoom, pan, and mouse modes', link: '/en/guide/zoom-and-pan' },
              { text: 'Files and playlists', link: '/en/guide/playlist' },
              { text: 'Audio, subtitles, and playback', link: '/en/guide/media' },
              { text: 'Screenshots and settings', link: '/en/guide/settings' },
            ],
          },
          {
            text: 'Development',
            items: [
              { text: 'Build and run', link: '/en/development/build' },
              { text: 'Portable packages and releases', link: '/en/development/packaging' },
              { text: 'Source and configuration', link: '/en/development/reference' },
              { text: 'Documentation maintenance', link: '/en/development/docs' },
            ],
          },
        ],
      },
    },
  },
  themeConfig: {
    logo: '/zopana.svg',
    search: {
      provider: 'local',
      options: {
        miniSearch: {
          searchOptions: { combineWith: 'AND' },
          options: {
            // Use the same Chinese bigrams in the build and in the browser.
            tokenize: (text) => text.split(/(\p{Script=Han}+)/u).flatMap((part, index) => {
              if (index % 2 === 0) return part.match(/[\p{L}\p{N}]+/gu) ?? []

              const characters = Array.from(part)
              if (characters.length === 1) return characters

              return characters.slice(1).map((character, offset) => characters[offset] + character)
            }),
          },
        },
        locales: {
          root: {
            translations: {
              button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' },
              modal: {
                displayDetails: '显示详细结果',
                resetButtonTitle: '清空搜索',
                backButtonTitle: '返回',
                noResultsText: '没有找到结果',
                footer: {
                  selectText: '打开',
                  navigateText: '切换',
                  closeText: '关闭',
                },
              },
            },
          },
        },
      },
    },
  },
})
