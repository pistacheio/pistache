import React from 'react';
import clsx from 'clsx';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import useBaseUrl from '@docusaurus/useBaseUrl';
import styles from './styles.module.css';

const features = [
  {
    title: 'Modern API',
    imageUrl: 'img/undraw_futuristic_interface.svg',
    description: (
      <>
        Written in pure C++17 and providing a low-level HTTP abstraction,
        Pistache makes playing with its modern API fun and easy,
        just take a look at the quickstart
      </>
    ),
  },
  {
    title: 'What\'s in the box',
    imageUrl: 'img/undraw_accept_request.svg',
    description: (
      <>
        <ul>
          <li>A multi-threaded HTTP server to build your APIs</li>
          <li>An asynchronous HTTP client to request APIs</li>
          <li>An HTTP router to dispatch requests to C++ functions</li>
          <li>A REST description DSL to easily define your APIs</li>
          <li>Type-safe headers and MIME types implementation</li>
        </ul>
      </>
    ),
  },
  {
    title: 'Use it',
    imageUrl: 'img/undraw_version_control.svg',
    description: (
      <>
        <ul>
          <li>Clone it on <a href="https://github.com/pistacheio/pistache">GitHub</a></li>
          <li>Start with the <a href="docs/">quickstart</a></li>
          <li>Read the full user's <a href="docs/http-handler">guide</a></li>
          <li>Have issues with it? Fill an <a href="https://github.com/pistacheio/pistache/issues">issue</a></li>
        </ul>
      </>
    ),
  },
];

function Feature({imageUrl, title, description}) {
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className={clsx('col col--4', styles.feature)}>
      {imgUrl && (
        <div className="text--center">
          <img className={styles.featureImage} src={imgUrl} alt={title} />
        </div>
      )}
      <h3>{title}</h3>
      <p>{description}</p>
    </div>
  );
}

function Home() {
  const context = useDocusaurusContext();
  const {siteConfig = {}} = context;
  return (
    <Layout
      description="Description will go into a meta tag in <head />">
      <header className={clsx('hero hero--primary', styles.heroBanner)}>
        <div className="container">
          <h1 className="hero__title">{siteConfig.title}</h1>
          <p className="hero__subtitle">{siteConfig.tagline}</p>
          <div className={styles.buttons}>
            <Link
              className={clsx(
                'button button--outline button--secondary button--lg',
                styles.getStarted,
              )}
              to={useBaseUrl('docs/')}>
              Get Started
            </Link>
          </div>
        </div>
      </header>
      <main>
        {features && features.length > 0 && (
          <section className={styles.features}>
            <div className="container">
              <div className="row">
                {features.map((props, idx) => (
                  <Feature key={idx} {...props} />
                ))}
              </div>
            </div>
          </section>
        )}
      </main>
    </Layout>
  );
}

export default Home;
