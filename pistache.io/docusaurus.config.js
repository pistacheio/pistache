module.exports = {
  title: 'Pistache',
  tagline: 'An elegant C++ REST framework.',
  url: 'https://pistache.io',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',
  favicon: 'img/favicon.ico',
  organizationName: 'pistacheio', // Usually your GitHub org/user name.
  projectName: 'pistace', // Usually your repo name.
  themeConfig: {
    navbar: {
      title: 'Pistache',
      logo: {
        alt: 'Pistache logo',
        src: 'img/logo.png',
      },
      items: [
        {
          to: 'docs/',
          activeBasePath: 'docs',
          label: 'Docs',
          position: 'left',
        },
        {
          href: 'https://github.com/pistacheio/pistache',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Docs',
          items: [
            {
              label: 'Style Guide',
              to: 'docs/',
            },
            {
              label: 'Second Doc',
              to: 'docs/doc2/',
            },
          ],
        },
        {
          title: 'Community',
          items: [
            {
              label: 'Stack Overflow',
              href: 'https://stackoverflow.com/questions/tagged/pistache',
            },
            {
              label: 'Discord',
              href: 'https://discord.com/invite/pistacheio',
            },
            {
              label: 'Twitter',
              href: 'https://twitter.com/pistacheio',
            },
          ],
        },
        {
          title: 'More',
          items: [
            {
              label: 'Blog',
              to: 'blog',
            },
            {
              label: 'GitHub',
              href: 'https://github.com/pistacheio/pistache',
            },
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} Pistache. Built by Tachi with <3 and Docusaurus.`,
    },
  },
  presets: [
    [
      '@docusaurus/preset-classic',
      {
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          // Please change this to your repo.
          editUrl:
            'https://github.com/pistacheio/pistache/edit/master/pistache.io/',
        },
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      },
    ],
  ],
};
