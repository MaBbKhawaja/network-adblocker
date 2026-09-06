#pragma once
#include <Arduino.h>
// ---------------- YouTube guard: rules for the browser extension in ../extension ----------------
// Served at GET /api/yt/rules so every browser in the house runs the same list, and picks up changes
// within 5 minutes of a reflash. Bump YT_RULES_VERSION whenever you edit the body. Keep it valid JSON
// (the body is the inside of an object; the firmware adds "version" and the braces).
//
//   prune_keys        object keys deleted wherever they appear in YouTube's JSON (the ad payloads)
//   prune_renderers   list items carrying one of these keys are removed (promoted tiles, nag dialog)
//   hide              CSS selectors hidden in the page
//   skip_buttons      clicked while an ad is showing in the player
//   ad_player_classes classes on #movie_player that mean "an ad is playing"
//   enforcement       the "ad blockers are not allowed" dialog, closed when seen
static const int YT_RULES_VERSION = 1;
static const char YT_RULES_BODY[] PROGMEM = R"json("prune_keys":["playerAds","adPlacements","adSlots","adBreakHeartbeatParams","adBreakParams"],
"prune_renderers":["adSlotRenderer","promotedSparklesWebRenderer","promotedSparklesTextSearchRenderer","displayAdRenderer","bannerPromoRenderer","statementBannerRenderer","promotedVideoRenderer","compactPromotedVideoRenderer","inFeedAdLayoutRenderer","mastheadAdRenderer","actionCompanionAdRenderer","searchPyvRenderer","adsEngagementPanelContentRenderer","enforcementMessageViewModel"],
"hide":["#masthead-ad","#player-ads","ytd-ad-slot-renderer","ytd-in-feed-ad-layout-renderer","ytd-promoted-sparkles-web-renderer","ytd-display-ad-renderer","ytd-banner-promo-renderer","ytd-statement-banner-renderer","ytd-promoted-video-renderer","ytd-compact-promoted-video-renderer","ytd-action-companion-ad-renderer","ytd-merch-shelf-renderer","ytd-rich-item-renderer:has(ytd-ad-slot-renderer)","ytd-rich-section-renderer:has(ytd-statement-banner-renderer)","ytd-engagement-panel-section-list-renderer[target-id=\"engagement-panel-ads\"]",".ytp-ad-overlay-container",".ytp-ad-text-overlay",".ytp-ad-image-overlay","ytd-popup-container:has(ytd-enforcement-message-view-model)","tp-yt-paper-dialog:has(ytd-enforcement-message-view-model)","ytm-promoted-video-renderer","ytm-companion-ad-renderer","ytm-promoted-sparkles-web-renderer"],
"skip_buttons":[".ytp-skip-ad-button",".ytp-ad-skip-button",".ytp-ad-skip-button-modern",".ytp-ad-skip-button-slot button"],
"ad_player_classes":["ad-showing","ad-interrupting"],
"enforcement":["ytd-enforcement-message-view-model"])json";
