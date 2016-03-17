# Custom Exchange Connectors and configuration

A bunch of custom exchange connectors have been developed for Jamloop. This document
presents them as well as the configuration that is needed to use them.

Please consult [Exchange specific configuration](https://github.com/rtbkit/rtbkit/wiki/Exchange-specific-configuration)
for more insight.

## Tremor

### Agent

| Field |  Type  |                      Description                       | Required |
|-------|--------|--------------------------------------------------------|----------|
| seat  | string | ID of the bidder seat on whose behalf this bid is made | No       |

### Creative

|  Field  |       Type      |                              Description                              | Required |
|---------|-----------------|-----------------------------------------------------------------------|----------|
| adm     | string          | Actual ad markup. XHTML if a response to a banner object, or VAST XML | No       |
| nurl    | string          | Win notice URL                                                        | No       |
| iurl    | string          | Sample image URL (without cache busting) for content checking         | No       |
| adomain | array of string | Advertiser’s primary or top-level domain for advertiser checking      | Yes      |
| attr    | array of attr   | Array of creative attributes                                          | No       |


## BrightRoll

### Agent

| Field |     Type     |                       Description                        | Required |
|-------|--------------|----------------------------------------------------------|----------|
| seat  | alphanumeric | ID provided by the bidder representing the buying entity | Yes      |

### Creative

|       Field       |  Type  |                             Description                             | Required |
|-------------------|--------|---------------------------------------------------------------------|----------|
| nurl              | string | The VAST tag to serve if the bid wins the BrightRoll auction        | Yes      |
| adomain           | string | Advertiser’s primary or top-level domain(s) for advertiser checking | Yes      |
| campaign_name     | string | Friendly creative name                                              | Yes      |
| line_item_name    | string | Friendly line item name                                             | Yes      |
| creative_name     | string | Friendly creative name                                              | Yes      |
| creative_duration | int    | Duration of the creative returned in seconds                        | Yes      |
| media_desc        | object | Object describing the media file returned in the VAST of the nurl   | Yes      |
| api               | int    | API framework required by the returned creative                     | Yes      |
| lid               | string | Line item ID of the returned creative                               | Yes      |
| landingpage_url   | string | Landing page URL for the campaign                                   | Yes      |
| advertiser_name   | string | Advertiser name                                                     | Yes      |
| companiontype     | int    | Companion types in the returned creative.                           | No       |
| adtype            | string | Defines if the bid is for a **video** or **banner** object          | Yes      |

#### MediaDesc

|     Field     |  Type  |                            Description                            | Required |
|---------------|--------|-------------------------------------------------------------------|----------|
| media_mime    | string | Mime type of the media file associated with the returned creative | Yes      |
| media_bitrate | int    | If the media file is a video, provide the associated bitrate      | Yes      |

## LiveRail

### Agent

| Field |  Type  |            Description            | Required |
|-------|--------|-----------------------------------|----------|
| seat  | string | Your seat ID provided by LiveRail | Yes      |

### Creative

|  Field  |       Type      |                    Description                     | Required |
|---------|-----------------|----------------------------------------------------|----------|
| adm     | string          | This a complete and valid VAST XML document inline | Yes      |
| adomain | array of string | The advertiser landing page                        | Yes      |
| buyerid | string          | LiveRail provided buyer id                         | No       |

## Adaptv

### Agent

| Field |  Type  |                      Description                       | Required |
|-------|--------|--------------------------------------------------------|----------|
| seat  | string | ID of the bidder seat on whose behalf this bid is made | No       |

### Creative

| Field |  Type  |                      Description                       | Required |
|-------|--------|--------------------------------------------------------|----------|
| adid  | string | ID that references the ad to be served if the bid wins | No       |
| nurl  | string | Win notice URL called if the bid wins                  | No       |
| adm   | string | VAST XML ad markup for the Video Object                | Yes      |

## Spotx

### Agent

|  Field   |  Type  |                      Description                       | Required |
|----------|--------|--------------------------------------------------------|----------|
| seat     | string | ID of the bidder seat on whose behalf this bid is made | Yes      |
| bidid    | string |                                                        | Yes      |
| seatName | string |                                                        | No       |

### Creative

|  Field  |   Type   |                       Description                        | Required |
|---------|----------|----------------------------------------------------------|----------|
| adm     | string   | VAST XML ad markup for the Video Object, need $MBR macro | Yes      |
| adid    | string   | ID that references the ad to be served if the bid wins   | Yes      |
| adomain | [string] | The advertiser landing page                              | Yes      |
