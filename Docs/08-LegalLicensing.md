# Branded cars, tracks, and asset licensing

This is a production risk register, not legal advice. Obtain qualified counsel and written agreements.

## Default rule

Until a written license is signed and recorded, use:

- an original project name;
- an original unbranded vehicle with a non-infringing silhouette;
- an original circuit, scenery, signage, and sponsor set;
- original or properly licensed audio;
- internally created or properly licensed models/textures/materials.

Do not assume that omitting a badge makes a copied vehicle shape safe. Do not assume that a real circuit's public visibility makes its layout, branding, scan data, signage, or commercial use unrestricted.

## Vehicle license matrix

For each real vehicle, confirm in writing:

- manufacturer and model names;
- trademarks, badges, logos, fonts, and model numbers;
- exterior shape/trade dress and interior design;
- CAD, scan, dimensional, performance, and setup data;
- paint colors, options, and configurator references;
- race livery, team marks, number, and sponsor marks;
- engine/exhaust/intake recordings and any supplied media;
- damage depiction, modification limits, and competitive comparison rules;
- territories, platforms, streaming/cloud distribution, term, and sublicensing;
- marketing screenshots, trailers, store art, social content, and press use;
- approval rounds, lead times, revocation/expiration, and archival obligations.

## Circuit license matrix

Confirm:

- venue/circuit name and marks;
- track geometry and survey/scan data rights;
- buildings, sculptures, distinctive structures, signage, sponsors, and event branding;
- photography, drone, LiDAR, photogrammetry, map, and satellite data rights;
- historic/current layout and modification permissions;
- territories, platforms, term, marketing, approval, and required credits.

## Third-party assets

For every marketplace or contractor asset, capture:

- asset title/version and supplier;
- purchaser/licensee entity;
- exact license and date acquired;
- permitted commercial use and modification;
- redistribution/source-file restrictions;
- seat/project restrictions;
- attribution/notice requirements;
- generative-AI restrictions where applicable;
- proof location and expiration/termination terms.

A marketplace listing is not proof that a model includes manufacturer rights.

## Prohibited sourcing

- game ripping or extraction;
- decompiling another title or converting its models/tracks;
- copying a manufacturer's configurator assets;
- tracing or photogrammetry from media without rights;
- scraping commercial model sites;
- reusing online videos as engine audio;
- removing watermarks or attribution;
- relying on "fan use" or "editorial use" for a commercial game.

## License-ready implementation

Keep brand-facing data separate from gameplay:

- vehicle identity and display strings in DataAssets/localization;
- meshes/materials/audio in a replaceable licensed content package;
- tune/spec data versioned independently;
- sponsor/livery decals in approved packs;
- build feature flags that exclude all restricted content;
- automated audit that fails public builds when a required ledger status is not `APPROVED`.
