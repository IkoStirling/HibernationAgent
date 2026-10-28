import assert from 'node:assert/strict';
import {usageOf,summarize} from '../tools/CacheEventLab.mjs';
assert.deepEqual(usageOf({usage:{input_tokens:100,output_tokens:12,input_tokens_details:{cached_tokens:64}}}),{input:100,output:12,hit:64,miss:36,ratio:.64});
assert.deepEqual(usageOf({usage:{prompt_tokens:100,completion_tokens:12,prompt_cache_hit_tokens:64,prompt_cache_miss_tokens:36}}),{input:100,output:12,hit:64,miss:36,ratio:.64});
assert.equal(usageOf({usage:{input_tokens:100,output_tokens:12}}).ratio,null);
assert.equal(usageOf({usage:{input_tokens:100,prompt_cache_hit_tokens:128}}).ratio,null);
assert.equal(usageOf({usage:{input_tokens:100,prompt_cache_hit_tokens:64,prompt_cache_miss_tokens:64}}).ratio,null);
const small={usage:{input:100,output:10,hit:90,miss:10},elapsed_ms:10};
const large={usage:{input:900,output:10,hit:90,miss:810},elapsed_ms:30};
const measured=summarize([small,large]);
assert.equal(measured.hit_ratio,.18); // Weighted by tokens, not the mean of 90% and 10%.
assert.equal(measured.input_tokens,1000);
const missing=summarize([small,{usage:null,elapsed_ms:5}]);
assert.equal(missing.cache_reported_calls,1);assert.equal(missing.input_tokens,null);assert.equal(missing.output_tokens,null);
assert.equal(missing.input_reported_calls,1);assert.equal(missing.semantic_passes,null);
assert.equal(summarize([]).hit_ratio,null);
console.log('Cache experiment metrics: provider variants, missing/inconsistent usage and weighted cache totals passed.');
