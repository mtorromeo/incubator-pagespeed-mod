/*
 * Copyright 2010 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "net/instaweb/rewriter/public/rewrite_options.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "net/instaweb/rewriter/public/domain_lawyer.h"
#include "net/instaweb/rewriter/public/file_load_policy.h"
#include "net/instaweb/util/public/basictypes.h"
#include "net/instaweb/util/public/message_handler.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"
#include "net/instaweb/util/public/wildcard_group.h"

namespace net_instaweb {

// TODO(jmarantz): consider merging this threshold with the image-inlining
// threshold, which is currently defaulting at 2000, so we have a single
// byte-count threshold, above which inlined resources get outlined, and
// below which outlined resources get inlined.
//
// TODO(jmarantz): user-agent-specific selection of inline threshold so that
// mobile phones are more prone to inlining.
//
// Further notes; jmaessen says:
//
// I suspect we do not want these bounds to match, and inlining for
// images is a bit more complicated because base64 encoding inflates
// the byte count of data: urls.  This is a non-issue for other
// resources (there may be some weirdness with iframes I haven't
// thought about...).
//
// jmarantz says:
//
// One thing we could do, if we believe they should be conceptually
// merged, is in image_rewrite_filter you could apply the
// base64-bloat-factor before comparing against the threshold.  Then
// we could use one number if we like that idea.
//
// jmaessen: For the moment, there's a separate threshold for image inline.
const int64 RewriteOptions::kDefaultCssInlineMaxBytes = 2048;
const int64 RewriteOptions::kDefaultImageInlineMaxBytes = 2048;
const int64 RewriteOptions::kDefaultJsInlineMaxBytes = 2048;
const int64 RewriteOptions::kDefaultCssOutlineMinBytes = 3000;
const int64 RewriteOptions::kDefaultJsOutlineMinBytes = 3000;

const int64 RewriteOptions::kDefaultHtmlCacheTimeMs = 0;
const int64 RewriteOptions::kDefaultCacheInvalidationTimestamp = -1;

// Limit on concurrent ongoing image rewrites.
// TODO(jmaessen): Determine a sane default for this value.
const int RewriteOptions::kDefaultImageMaxRewritesAtOnce = 8;

// IE limits URL size overall to about 2k characters.  See
// http://support.microsoft.com/kb/208427/EN-US
const int RewriteOptions::kMaxUrlSize = 2083;

// See http://code.google.com/p/modpagespeed/issues/detail?id=9.  By
// default, Apache evidently limits each URL path segment (between /)
// to about 256 characters.  This is not a fundamental URL limitation
// but is Apache specific.  Ben Noordhuis has provided a workaround
// of hooking map_to_storage to skip the directory-mapping phase in
// Apache.  See http://code.google.com/p/modpagespeed/issues/detail?id=176
const int RewriteOptions::kDefaultMaxUrlSegmentSize = 1024;

const GoogleString RewriteOptions::kDefaultBeaconUrl =
    "/mod_pagespeed_beacon?ets=";

bool RewriteOptions::ParseRewriteLevel(
    const StringPiece& in, RewriteLevel* out) {
  bool ret = false;
  if (in != NULL) {
    if (StringCaseEqual(in, "CoreFilters")) {
      *out = kCoreFilters;
      ret = true;
    } else if (StringCaseEqual(in, "PassThrough")) {
      *out = kPassThrough;
      ret = true;
    } else if (StringCaseEqual(in, "TestingCoreFilters")) {
      *out = kTestingCoreFilters;
      ret = true;
    } else if (StringCaseEqual(in, "AllFilters")) {
      *out = kAllFilters;
      ret = true;
    }
  }
  return ret;
}

RewriteOptions::RewriteOptions()
    : modified_(false),
      level_(kPassThrough),
      css_inline_max_bytes_(kDefaultCssInlineMaxBytes),
      image_inline_max_bytes_(kDefaultImageInlineMaxBytes),
      js_inline_max_bytes_(kDefaultJsInlineMaxBytes),
      css_outline_min_bytes_(kDefaultCssInlineMaxBytes),
      js_outline_min_bytes_(kDefaultJsInlineMaxBytes),
      html_cache_time_ms_(kDefaultHtmlCacheTimeMs),
      beacon_url_(kDefaultBeaconUrl),
      image_max_rewrites_at_once_(kDefaultImageMaxRewritesAtOnce),
      max_url_segment_size_(kDefaultMaxUrlSegmentSize),
      max_url_size_(kMaxUrlSize),
      enabled_(true),
      botdetect_enabled_(false),
      combine_across_paths_(true),
      log_rewrite_timing_(false),
      lowercase_html_names_(false),
      always_rewrite_css_(false),
      respect_vary_(false),
      cache_invalidation_timestamp_(kDefaultCacheInvalidationTimestamp) {
  // TODO(jmarantz): If we instantiate many RewriteOptions, this should become a
  // public static method called once at startup.
  SetUp();

  all_options_.push_back(&level_);
  all_options_.push_back(&css_inline_max_bytes_);
  all_options_.push_back(&image_inline_max_bytes_);
  all_options_.push_back(&js_inline_max_bytes_);
  all_options_.push_back(&css_outline_min_bytes_);
  all_options_.push_back(&js_outline_min_bytes_);
  all_options_.push_back(&html_cache_time_ms_);
  all_options_.push_back(&beacon_url_);
  all_options_.push_back(&image_max_rewrites_at_once_);
  all_options_.push_back(&max_url_segment_size_);
  all_options_.push_back(&max_url_size_);
  all_options_.push_back(&enabled_);
  all_options_.push_back(&botdetect_enabled_);
  all_options_.push_back(&combine_across_paths_);
  all_options_.push_back(&log_rewrite_timing_);
  all_options_.push_back(&lowercase_html_names_);
  all_options_.push_back(&always_rewrite_css_);
  all_options_.push_back(&respect_vary_);
  all_options_.push_back(&cache_invalidation_timestamp_);
}

RewriteOptions::~RewriteOptions() {
}

RewriteOptions::OptionBase::~OptionBase() {
}

void RewriteOptions::SetUp() {
  name_filter_map_["add_head"] = kAddHead;
  name_filter_map_["add_instrumentation"] = kAddInstrumentation;
  name_filter_map_["collapse_whitespace"] = kCollapseWhitespace;
  name_filter_map_["combine_css"] = kCombineCss;
  name_filter_map_["combine_javascript"] = kCombineJavascript;
  name_filter_map_["combine_heads"] = kCombineHeads;
  name_filter_map_["convert_jpeg_to_webp"] = kConvertJpegToWebp;
  name_filter_map_["div_structure"] = kDivStructure;
  name_filter_map_["elide_attributes"] = kElideAttributes;
  name_filter_map_["extend_cache"] = kExtendCache;
  name_filter_map_["flush_html"] = kFlushHtml;
  name_filter_map_["inline_css"] = kInlineCss;
  name_filter_map_["inline_images"] = kInlineImages;
  name_filter_map_["inline_javascript"] = kInlineJavascript;
  name_filter_map_["insert_img_dimensions"] =
      kInsertImageDimensions;  // Deprecated due to spelling.
  name_filter_map_["insert_image_dimensions"] = kInsertImageDimensions;
  name_filter_map_["left_trim_urls"] = kLeftTrimUrls;  // Deprecated
  name_filter_map_["make_google_analytics_async"] = kMakeGoogleAnalyticsAsync;
  name_filter_map_["move_css_to_head"] = kMoveCssToHead;
  name_filter_map_["outline_css"] = kOutlineCss;
  name_filter_map_["outline_javascript"] = kOutlineJavascript;
  name_filter_map_["recompress_images"] = kRecompressImages;
  name_filter_map_["remove_comments"] = kRemoveComments;
  name_filter_map_["remove_quotes"] = kRemoveQuotes;
  name_filter_map_["resize_images"] = kResizeImages;
  name_filter_map_["rewrite_css"] = kRewriteCss;
  name_filter_map_["rewrite_domains"] = kRewriteDomains;
  name_filter_map_["rewrite_javascript"] = kRewriteJavascript;
  name_filter_map_["rewrite_style_attributes"] = kRewriteStyleAttributes;
  name_filter_map_["rewrite_style_attributes_with_url"] =
      kRewriteStyleAttributesWithUrl;
  name_filter_map_["sprite_images"] = kSpriteImages;
  name_filter_map_["strip_scripts"] = kStripScripts;
  name_filter_map_["trim_urls"] = kLeftTrimUrls;

  // Create filter sets for compound filter flags
  // (right now this is just rewrite_images)
  // TODO(jmaessen): add kConvertJpegToWebp here when it becomes part of
  // rewrite_images.
  name_filter_set_map_["rewrite_images"].insert(kInlineImages);
  name_filter_set_map_["rewrite_images"].insert(kInsertImageDimensions);
  name_filter_set_map_["rewrite_images"].insert(kRecompressImages);
  name_filter_set_map_["rewrite_images"].insert(kResizeImages);

  // Create an empty set for the pass-through level.
  level_filter_set_map_[kPassThrough];

  // Core filter level includes the "core" filter set.
  // TODO(jmaessen): add kConvertJpegToWebp here when it becomes part of
  // rewrite_images.
  level_filter_set_map_[kCoreFilters].insert(kAddHead);
  level_filter_set_map_[kCoreFilters].insert(kCombineCss);
  level_filter_set_map_[kCoreFilters].insert(kExtendCache);
  level_filter_set_map_[kCoreFilters].insert(kInlineCss);
  level_filter_set_map_[kCoreFilters].insert(kInlineImages);
  level_filter_set_map_[kCoreFilters].insert(kInlineJavascript);
  level_filter_set_map_[kCoreFilters].insert(kInsertImageDimensions);
  level_filter_set_map_[kCoreFilters].insert(kLeftTrimUrls);
  level_filter_set_map_[kCoreFilters].insert(kRecompressImages);
  level_filter_set_map_[kCoreFilters].insert(kResizeImages);
  level_filter_set_map_[kCoreFilters].insert(kRewriteCss);
  level_filter_set_map_[kCoreFilters].insert(kRewriteJavascript);

  // Copy CoreFilters set into TestingCoreFilters set ...
  level_filter_set_map_[kTestingCoreFilters] =
      level_filter_set_map_[kCoreFilters];
  // ... and add possibly unsafe filters.
  // TODO(jmarantz): Migrate these over to CoreFilters.
  level_filter_set_map_[kTestingCoreFilters].insert(kConvertJpegToWebp);
  level_filter_set_map_[kTestingCoreFilters].insert(kFlushHtml);
  level_filter_set_map_[kTestingCoreFilters].insert(kMakeGoogleAnalyticsAsync);
  level_filter_set_map_[kTestingCoreFilters].insert(kRewriteDomains);

  // Set complete set for all filters set.
  for (int f = kFirstFilter; f != kLastFilter; ++f) {
    level_filter_set_map_[kAllFilters].insert(static_cast<Filter>(f));
  }
}

bool RewriteOptions::EnableFiltersByCommaSeparatedList(
    const StringPiece& filters, MessageHandler* handler) {
  return AddCommaSeparatedListToFilterSet(
      filters, handler, &enabled_filters_);
}

bool RewriteOptions::DisableFiltersByCommaSeparatedList(
    const StringPiece& filters, MessageHandler* handler) {
  return AddCommaSeparatedListToFilterSet(
      filters, handler, &disabled_filters_);
}

void RewriteOptions::DisableAllFiltersNotExplicitlyEnabled() {
  for (int f = kFirstFilter; f != kLastFilter; ++f) {
    Filter filter = static_cast<Filter>(f);
    if (enabled_filters_.find(filter) == enabled_filters_.end()) {
      DisableFilter(filter);
    }
  }
}

void RewriteOptions::EnableFilter(Filter filter) {
  std::pair<FilterSet::iterator, bool> inserted =
      enabled_filters_.insert(filter);
  modified_ |= inserted.second;
}

void RewriteOptions::DisableFilter(Filter filter) {
  std::pair<FilterSet::iterator, bool> inserted =
      disabled_filters_.insert(filter);
  modified_ |= inserted.second;
}

bool RewriteOptions::AddCommaSeparatedListToFilterSet(
    const StringPiece& filters, MessageHandler* handler, FilterSet* set) {
  StringPieceVector names;
  SplitStringPieceToVector(filters, ",", &names, true);
  bool ret = true;
  for (int i = 0, n = names.size(); i < n; ++i) {
    GoogleString option(names[i].data(), names[i].size());
    NameToFilterMap::iterator p = name_filter_map_.find(option);
    if (p == name_filter_map_.end()) {
      // Handle a compound filter name.  This is much less common.
      NameToFilterSetMap::iterator s = name_filter_set_map_.find(option);
      if (s == name_filter_set_map_.end()) {
        handler->Message(kWarning, "Invalid filter name: %s", option.c_str());
        ret = false;
      } else {
        const FilterSet& new_flags = s->second;
        // Insert all new_flags into set.
        FilterSet::const_iterator end = new_flags.end();
        for (FilterSet::const_iterator j = new_flags.begin(); j != end; ++j) {
          std::pair<FilterSet::iterator, bool> inserted = set->insert(*j);
          modified_ |= inserted.second;
        }
      }
    } else {
      std::pair<FilterSet::iterator, bool> inserted = set->insert(p->second);
      modified_ |= inserted.second;
    }
  }
  return ret;
}

bool RewriteOptions::Enabled(Filter filter) const {
  if (disabled_filters_.find(filter) != disabled_filters_.end()) {
    return false;
  }

  RewriteLevelToFilterSetMap::const_iterator it =
      level_filter_set_map_.find(level_.value());
  if (it != level_filter_set_map_.end()) {
    const FilterSet& filters = it->second;
    if (filters.find(filter) != filters.end()) {
      return true;
    }
  }

  return (enabled_filters_.find(filter) != enabled_filters_.end());
}

void RewriteOptions::Merge(const RewriteOptions& first,
                           const RewriteOptions& second) {
  modified_ = first.modified_ || second.modified_;
  enabled_filters_ = first.enabled_filters_;
  disabled_filters_ = first.disabled_filters_;
  for (FilterSet::const_iterator p = second.enabled_filters_.begin(),
           e = second.enabled_filters_.end(); p != e; ++p) {
    Filter filter = *p;
    // Enabling in 'second' trumps Disabling in first.
    disabled_filters_.erase(filter);
    enabled_filters_.insert(filter);
  }

  for (FilterSet::const_iterator p = second.disabled_filters_.begin(),
           e = second.disabled_filters_.end(); p != e; ++p) {
    Filter filter = *p;
    // Disabling in 'second' trumps enabling in anything.
    disabled_filters_.insert(filter);
    enabled_filters_.erase(filter);
  }

  for (int i = 0, n = all_options_.size(); i < n; ++i) {
    all_options_[i]->Merge(first.all_options_[i], second.all_options_[i]);
  }

  // Pick the larger of the two cache invalidation timestamps. Following
  // calculation assumes the default value of cache invalidation timestamp
  // to be -1.
  //
  // Note: this gets merged by order in the above loop, and then this
  // block of code overrides the merged value.
  //
  // TODO(jmarantz): fold this logic into a new OptionBase subclass whose
  // Merge method does the right thing.
  if (first.cache_invalidation_timestamp_.value() !=
      RewriteOptions::kDefaultCacheInvalidationTimestamp ||
      second.cache_invalidation_timestamp_.value() !=
          RewriteOptions::kDefaultCacheInvalidationTimestamp) {
    cache_invalidation_timestamp_.set(
        std::max(first.cache_invalidation_timestamp_.value(),
                 second.cache_invalidation_timestamp_.value()));
  }

  // Note that the domain-lawyer merge works one-at-a-time, which is easier
  // to unit test.  So we have to call it twice.
  domain_lawyer_.Merge(first.domain_lawyer_);
  domain_lawyer_.Merge(second.domain_lawyer_);

  file_load_policy_.Merge(first.file_load_policy_);
  file_load_policy_.Merge(second.file_load_policy_);

  allow_resources_.CopyFrom(first.allow_resources_);
  allow_resources_.AppendFrom(second.allow_resources_);

  retain_comments_.CopyFrom(first.retain_comments_);
  retain_comments_.AppendFrom(second.retain_comments_);
}

}  // namespace net_instaweb
