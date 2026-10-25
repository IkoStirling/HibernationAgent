"""Approximate Mandarin readings as Japanese kana; never translates dialogue."""
from pypinyin import lazy_pinyin, Style
ROWS = {
    "b": ("バ", "ボ", "ベ", "ビ", "ブ"),
    "p": ("パ", "ポ", "ペ", "ピ", "プ"),
    "m": ("マ", "モ", "メ", "ミ", "ム"),
    "f": ("ファ", "フォ", "フェ", "フィ", "フ"),
    "d": ("ダ", "ド", "デ", "ディ", "ドゥ"),
    "t": ("タ", "ト", "テ", "ティ", "トゥ"),
    "n": ("ナ", "ノ", "ネ", "ニ", "ヌ"),
    "l": ("ラ", "ロ", "レ", "リ", "ル"),
    "g": ("ガ", "ゴ", "ゲ", "ギ", "グ"),
    "k": ("カ", "コ", "ケ", "キ", "ク"),
    "h": ("ハ", "ホ", "ヘ", "ヒ", "フ"),
    "j": ("ジャ", "ジョ", "ジェ", "ジ", "ジュ"),
    "q": ("チャ", "チョ", "チェ", "チ", "チュ"),
    "x": ("シャ", "ショ", "シェ", "シ", "シュ"),
    "zh": ("ヂャ", "ヂョ", "ヂェ", "ヂ", "ヂュ"),
    "ch": ("チャ", "チョ", "チェ", "チ", "チュ"),
    "sh": ("シャ", "ショ", "シェ", "シ", "シュ"),
    "r": ("ラ", "ロ", "レ", "リ", "ル"),
    "z": ("ザ", "ゾ", "ゼ", "ズィ", "ズ"),
    "c": ("ツァ", "ツォ", "ツェ", "ツィ", "ツ"),
    "s": ("サ", "ソ", "セ", "スィ", "ス"),
    "": ("ア", "オ", "エ", "イ", "ウ"),
}
ZERO = dict(yi="i", ya="ia", ye="ie", yao="iao", you="iu", yan="ian",
            yin="in", yang="iang", ying="ing", yong="iong", yu="v",
            yue="ve", yuan="van", yun="vn", wu="u", wa="ua", wo="uo",
            wai="uai", wei="ui", wan="uan", wen="un", wang="uang", weng="ueng")
SPECIAL = {"ni": "ニー", "zhi": "ヂー", "chi": "チー", "shi": "シー",
           "ri": "リー", "zi": "ズー", "ci": "ツー", "si": "スー",
           "de": "ダ", "ge": "グァ", "le": "ラ", "er": "アル",
           "n": "ン", "ng": "ン", "m": "ム"}
ZERO_KANA = dict(yi="イ", ya="ヤ", ye="イェ", yao="ヤオ", you="ヨウ", yan="イェン",
                 yin="イン", yang="ヤン", ying="イン", yong="ヨン", yu="ユ",
                 yue="ユェ", yuan="ユェン", yun="ユン", wu="ウ", wa="ワ", wo="ウォ",
                 wai="ワイ", wei="ウェイ", wan="ワン", wen="ウェン", wang="ワン", weng="ウォン")
ALPHABET = dict(zip("abcdefghijklmnopqrstuvwxyz", ("エー", "ビー", "シー", "ディー", "イー", "エフ",
                "ジー", "エイチ", "アイ", "ジェー", "ケー", "エル", "エム", "エヌ", "オー", "ピー",
                "キュー", "アール", "エス", "ティー", "ユー", "ヴィー", "ダブリュー", "エックス", "ワイ", "ゼット")))
PUNCT = str.maketrans({"，": "、", ",": "、", ".": "。", "！": "！", "?": "？", ";": "、", "；": "、", ":": "、", "：": "、"})


def syllable(p):
    if p in ZERO_KANA:
        return ZERO_KANA[p]
    if p in SPECIAL:
        return SPECIAL[p]
    if p in ZERO:
        p = ZERO[p]
    initial = next((k for k in ("zh", "ch", "sh", *ROWS) if k and p.startswith(k)), "")
    final = p[len(initial):]
    a, o, e, i, u = ROWS[initial]
    if initial in ("j", "q", "x") and final.startswith("u"):
        final = "v" + final[1:]
    # Japanese palatal glides attach to the consonant's i-row kana.
    glide = {"ジ": "ジ", "チ": "チ", "シ": "シ", "ディ": "ディ", "ティ": "ティ"}.get(i, i)
    finals = {
        "a": a, "o": o, "e": e, "ai": a + "イ", "ei": e + "イ",
        "ao": a + "オ", "ou": o + "ウ", "an": a + "ン", "ang": a + "ン",
        "en": e + "ン", "eng": e + "ン", "ong": o + "ン", "i": i,
        "ia": glide + "ャ", "ie": glide + "ェ", "iao": glide + "ャオ",
        "iu": glide + "ョウ", "ian": glide + "ェン", "in": i + "ン",
        "iang": glide + "ャン", "ing": i + "ン", "iong": glide + "ョン",
        "u": u, "ua": u + "ァ", "uo": u + "ォ", "uai": u + "ァイ",
        "ui": u + "ェイ", "uan": u + "ァン", "un": u + "ェン",
        "uang": u + "ァン", "ueng": u + "ォン", "v": glide + "ュ",
        "ve": glide + "ュェ", "van": glide + "ュェン", "vn": glide + "ュン",
    }
    return finals.get(final, "")


def kana(text):
    digits = dict(zip("0123456789", "零一二三四五六七八九"))
    text = "".join(digits.get(c, c) for c in text)
    parts = lazy_pinyin(text, style=Style.NORMAL, v_to_u=False,
                        errors=lambda s: ["\0" + c for c in s])
    result = []
    for part in parts:
        if not part.startswith("\0"):
            result.append(syllable(part))
            continue
        raw = part[1:]
        if raw.lower() in ALPHABET:
            result.append(ALPHABET[raw.lower()])
        elif raw.isspace() or raw in "。，、！？!?.,;；:：" or "\u3040" <= raw <= "\u30ff":
            result.append(raw.translate(PUNCT))
    return "".join(result)


