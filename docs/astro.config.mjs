import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Deployed to Pages at https://kittipitch.github.io/smalltv-mod/
export default defineConfig({
  site: 'https://kittipitch.github.io',
  base: '/smalltv-mod',
  integrations: [
    starlight({
      title: 'smalltv-mod',
      description:
        'Open-source firmware for the GeekMagic SmallTV in its ESP8266 and ESP32-C2 versions: Claude usage meter, quota pages, agenda and weather, and plane radar.',
      logo: {
        src: './src/assets/logo.svg',
        replacesTitle: false,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/kittipitch/smalltv-mod',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/kittipitch/smalltv-mod/edit/main/docs/',
      },
      sidebar: [
        { label: 'Home', link: '/' },
        {
          label: 'Getting started',
          items: [
            { label: 'Hardware and variants', link: '/getting-started/hardware/' },
            { label: 'Flashing', link: '/getting-started/flashing/' },
            { label: 'First-time setup', link: '/getting-started/setup/' },
            { label: "Sharing your laptop's WiFi", link: '/getting-started/sharing-wifi/' },
            { label: 'Daemon setup, start to finish', link: '/getting-started/first-setup/' },
            { label: 'What the daemon needs installed', link: '/getting-started/daemon-requirements/' },
            { label: 'Google Calendar (service account)', link: '/getting-started/google-calendar/' },
            { label: 'Keep the daemon running', link: '/getting-started/keep-it-running/' },
          ],
        },
        {
          label: 'Features',
          items: [
            { label: 'Claude usage meter', link: '/features/usage/' },
            { label: 'Plane radar', link: '/features/radar/' },
            { label: 'Pictures', link: '/features/pictures/' },
          ],
        },
        {
          label: 'Reference',
          items: [
            { label: 'Building from source', link: '/reference/building/' },
            { label: 'Bringing up a new board', link: '/reference/new-boards/' },
            { label: 'Letting an agent drive a browser', link: '/reference/agent-browsers/' },
            { label: 'Recovery and credits', link: '/reference/recovery/' },
          ],
        },
      ],
    }),
  ],
});
