# Asset and license ledger

No external asset may be imported until this record is created. No restricted/brand-facing asset may enter a public build unless `Status` is `APPROVED` and the evidence has been reviewed by the legal owner.

## Status values

- `PROPOSED`
- `QUARANTINED`
- `REVIEWING`
- `APPROVED_INTERNAL_ONLY`
- `APPROVED`
- `REJECTED`
- `EXPIRED`

## Entry template

### ASSET-0000: descriptive name

- Status:
- Category: model / texture / material / scan / audio / font / plugin / code / brand / vehicle / circuit / livery / other
- Supplier/creator:
- Licensee legal entity:
- Source/proof location:
- Date acquired:
- Exact license/agreement/version:
- Permitted project/builds:
- Commercial use:
- Modification rights:
- Redistribution/source restrictions:
- Platforms and cloud/streaming rights:
- Territories:
- Term/expiration:
- Attribution/notices:
- Marketing/trailer/screenshot rights:
- Manufacturer/venue/sponsor approvals:
- Generative-AI restrictions:
- Internal package path:
- Reviewer and review date:
- Notes/obligations:

## Build audit requirements

The release pipeline should fail when:

- an imported external package has no ledger ID;
- a package marked `QUARANTINED`, `REJECTED`, or `EXPIRED` is referenced;
- a restricted package enters a build variant not named in its approval;
- required credit/notice metadata is missing;
- the license proof or approval record cannot be located.
