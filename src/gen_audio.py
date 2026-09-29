"""生成贪吃蛇音效。运行：python gen_audio.py"""
import wave, struct, math

def write_wav(path, samples, sr):
    """samples: -1.0~1.0 的浮点列表"""
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for s in samples:
            # 限幅，防止溢出破音
            v = int(max(-1.0, min(1.0, s)) * 32767)
            frames += struct.pack('<h', v)
        w.writeframes(bytes(frames))
    print(f'  已生成 {path}')

# ---------------------------------------------------------------- 吃掉音效
def make_eat(path):
    sr = 44100
    dur = 0.10                      # 0.1 秒，短促
    n = int(sr * dur)
    out = []
    for i in range(n):
        t = i / sr
        p = i / n                   # 0→1 进度

        # 音高上扬：880Hz → 1320Hz，有"收获"的上行感
        f = 880.0 * (1320.0 / 880.0) ** p

        # 两个八度叠加，声音更饱满
        s = 0.7 * math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * f * 2 * t)

        # 包络：3ms 淡入防爆音，之后指数衰减
        attack = min(1.0, t / 0.003)
        decay = math.exp(-9.0 * p)
        # 末尾 15% 再乘一次淡出，确保结尾归零
        tail = 1.0 if p < 0.85 else (1.0 - p) / 0.15

        out.append(s * attack * decay * tail * 0.55)
    write_wav(path, out, sr)

# ---------------------------------------------------------------- 背景音乐
# C 大调五声音阶（宫调式），不含半音，怎么弹都不会难听
SCALE = {'C': 261.63, 'D': 293.66, 'E': 329.63, 'G': 392.00, 'A': 440.00,
         'c': 523.25, 'd': 587.33, 'e': 659.25, 'g': 783.99, 'a': 880.00}

def make_bg(path):
    sr = 22050
    bpm = 100.0
    beat = 60.0 / bpm               # 0.6 秒一拍
    bars = 8                       # 8 小节
    beats_per_bar = 4
    total_beats = bars * beats_per_bar
    n = int(sr * total_beats * beat)

    # 每小节 4 个音，上行+回落，避免单调
    pattern = ['C', 'E', 'G', 'E']
    notes = []
    for b in range(bars):
        for k in range(beats_per_bar):
            # 第 4、8 小节换成 A 音收尾，制造段落感
            note = 'A' if (b in (3, 7) and k == 3) else pattern[k]
            notes.append(note)

    out = [0.0] * n
    for idx, name in enumerate(notes):
        f = SCALE[name]
        start = int(idx * beat * sr)
        dur = beat * 0.95
        ln = int(sr * dur)
        for i in range(ln):
            if start + i >= n:
                break
            t = i / sr
            p = i / ln
            # 钟琴式包络：快速起音 + 指数衰减
            env = min(1.0, t / 0.008) * math.exp(-4.5 * p) * (1.0 - p) ** 0.5
            # 基音 + 一个八度泛音，音色清亮
            s = 0.75 * math.sin(2 * math.pi * f * t) + 0.25 * math.sin(2 * math.pi * f * 2 * t)
            out[start + i] += s * env * 0.20

    # 低音持续音（C2），整首都垫着
    for i in range(n):
        t = i / sr
        out[i] += 0.55 * math.sin(2 * math.pi * 65.41 * t) * 0.16

    # 首尾各 50ms 淡入淡出到静音，循环接回去无跳变
    fade = int(sr * 0.05)
    for i in range(fade):
        g = i / fade
        out[i] *= g
        out[n - 1 - i] *= g

    write_wav(path, out, sr)
    print(f'    时长 {total_beats*beat:.1f} 秒，可无缝循环')

# ---------------------------------------------------------------- 背景音乐（多版本）
def make_bg_v2(path, bpm, pattern, key, bars, note_len, drone_f, mel_amp=0.20,
               drone_amp=0.16, drone_on=True, staccato=False, swing=False):
    """可调参数的背景音乐生成器。

    bpm        速度，越大越欢快
    pattern    每小节的音名列表，如 ['C','E','G','E']
    key        音名→频率表
    bars       小节数
    note_len   每个音占几拍（0.5 = 八分音符）
    drone_f    低音持续音频率
    staccato   True 则音更短促，制造跳跃感
    swing      True 则偶数拍延后半拍，产生摇摆节奏
    """
    sr = 22050
    beat = 60.0 / bpm
    beats_per_bar = len(pattern)
    total_beats = bars * beats_per_bar
    n = int(sr * total_beats * beat)

    # 排布音符起始时间
    events = []
    for b in range(bars):
        for k, name in enumerate(pattern):
            if name == '.':          # '.' 表示休止符
                continue
            pos = (b * beats_per_bar + k) * note_len
            if swing and k % 2 == 1:
                pos += note_len * 0.18   # 后半拍稍微延后 = 摇摆感
            events.append((pos, name))

    out = [0.0] * n
    for pos, name in events:
        f = key[name]
        ln = int(sr * note_len * beat)
        start = int(pos * beat * sr)
        if staccato:
            ln = int(ln * 0.55)      # 断奏：只发音符长度的一半多
        for i in range(ln):
            if start + i >= n:
                break
            t = i / sr
            p = i / ln
            # 起音快、衰减快；断奏用更陡的衰减制造颗粒感
            k_decay = 7.0 if staccato else 4.0
            env = min(1.0, t / 0.006) * math.exp(-k_decay * p) * (1.0 - p) ** 0.5
            s = 0.75 * math.sin(2 * math.pi * f * t) + 0.25 * math.sin(2 * math.pi * f * 2 * t)
            out[start + i] += s * env * mel_amp

    if drone_on:
        for i in range(n):
            t = i / sr
            out[i] += 0.55 * math.sin(2 * math.pi * drone_f * t) * drone_amp

    # 首尾各 50ms 淡入淡出到静音。
    # 不能只用交叉淡化——那样两端仍有非零值，接缝处会产生可听见的"咔"声。
    # 两端都归零，循环接回去时物理上不可能有跳变。
    fade = int(sr * 0.05)
    for i in range(fade):
        g = i / fade
        out[i] *= g                # 淡入
        out[n - 1 - i] *= g        # 淡出

    write_wav(path, out, sr)
    print(f'    时长 {total_beats*beat:.1f} 秒，{bpm:.0f} BPM')


# ---------------------------------------------------------------- 吃掉音效（多版本）
NOTE_HZ = {'A4': 440.00, 'C5': 523.25, 'D5': 587.33, 'E5': 659.25, 'G5': 783.99,
           'A5': 880.00, 'C6': 1046.50, 'D6': 1174.66, 'E6': 1318.51, 'G6': 1567.98}

def _env(t, ln, attack=0.003, decay=9.0, tail_ratio=0.15):
    """统一包络：快起 + 指数衰减 + 末尾归零，保证首尾都是 0 不出爆音"""
    p = t / ln
    attack_g = min(1.0, t / attack)
    decay_g = math.exp(-decay * p)
    tail_g = 1.0 if p < (1 - tail_ratio) else (1.0 - p) / tail_ratio
    return attack_g * decay_g * tail_g

def synth_glide(path, f0, f1, dur, amp=0.55, decay=9.0, harmonics=(0.7, 0.3)):
    """滑音：音高从 f0 线性升到 f1"""
    sr = 44100
    n = int(sr * dur)
    out = []
    for i in range(n):
        t = i / sr
        p = i / n
        f = f0 + (f1 - f0) * p
        s = harmonics[0] * math.sin(2 * math.pi * f * t)
        if len(harmonics) > 1:
            s += harmonics[1] * math.sin(2 * math.pi * f * 2 * t)
        out.append(s * _env(t, dur, decay=decay) * amp)
    write_wav(path, out, sr)

def synth_seq(path, notes, note_dur, amp=0.55, decay=7.0):
    """琶音：依次弹几个音，音尾刚好衔接"""
    sr = 44100
    n = int(sr * note_dur * len(notes))
    out = [0.0] * n
    for idx, hz in enumerate(notes):
        start = int(idx * note_dur * sr)
        ln = int(sr * note_dur)
        for i in range(ln):
            if start + i >= n:
                break
            t = i / sr
            s = 0.75 * math.sin(2 * math.pi * hz * t) + 0.25 * math.sin(2 * math.pi * hz * 2 * t)
            out[start + i] += s * _env(t, note_dur, decay=decay) * amp
    write_wav(path, out, sr)

def synth_click(path, dur=0.06, amp=1.6):
    """打击感：噪声 + 快速衰减，像敲一下木头。
    注意低通滤波会吃掉大量能量，amp 要比其他合成函数大得多，
    否则响度会明显低于其它音效。"""
    import random
    random.seed(7)
    sr = 44100
    n = int(sr * dur)
    out = []
    lp = 0.0
    for i in range(n):
        t = i / sr
        p = i / n
        # 噪声经过一阶低通，去掉刺耳的高频
        lp += (random.uniform(-1, 1) - lp) * 0.35
        tone = 0.4 * math.sin(2 * math.pi * 1800 * t)
        env = min(1.0, t / 0.001) * math.exp(-22.0 * p)
        out.append((lp * 0.6 + tone) * env * amp)
    write_wav(path, out, sr)


if __name__ == '__main__':
    print('生成中...')
    make_eat('eate_new.wav')

    print('  [吃掉音效多版本]')
    H = NOTE_HZ

    # 1 柔和单音：圆润不刺耳
    synth_glide('eate_soft.wav', H['A5'], H['A5'] * 1.001, 0.09,
                amp=0.5, decay=7.0, harmonics=(0.85, 0.15))
    print('    eate_soft.wav        柔和单音 A5，圆润')

    # 2 清脆双音：大幅度上扬
    synth_glide('eate_two_tone.wav', H['C5'], H['E6'], 0.09, amp=0.5, decay=10.0)
    print('    eate_two_tone.wav    清脆上扬 C5→E6')

    # 3 咚嘀上行：两个音分开弹
    synth_seq('eate_bounce.wav', [H['G5'], H['D6']], 0.045, amp=0.5)
    print('    eate_bounce.wav      咚嘀 G5→D6 两音')

    # 4 打击感：短促敲击
    synth_click('eate_click.wav')
    print('    eate_click.wav       打击感，短促敲击')

    # 5 快速上滑：嗖的一声
    synth_glide('eate_sweep.wav', H['A4'], H['G6'], 0.07, amp=0.45, decay=13.0)
    print('    eate_sweep.wav       快速上滑 嗖')

    # 6 低沉木块：闷一点，不抢背景音乐
    synth_seq('eate_woody.wav', [H['G5']], 0.075, amp=0.55, decay=12.0)
    print('    eate_woody.wav       低沉木块 G5，闷')

    # 原版：100 BPM 舒缓
    print('  [bg_new] 舒缓 100 BPM')
    make_bg('bg_new.wav')

    # 版本 2：欢快，140 BPM 大调琶音
    print('  [bg_fast] 欢快 140 BPM')
    make_bg_v2('bg_fast.wav', bpm=140, pattern=['C', 'E', 'G', 'c'], key=SCALE,
               bars=8, note_len=0.5, drone_f=65.41)

    # 版本 3：舒缓三拍子，90 BPM，音更长
    print('  [bg_slow] 舒缓三拍子 90 BPM')
    make_bg_v2('bg_slow.wav', bpm=90, pattern=['C', 'G', 'E'], key=SCALE,
               bars=6, note_len=1.0, drone_f=65.41, mel_amp=0.24)

    # 版本 4：电子游戏感，132 BPM，断奏 + 摇摆 + 休止
    print('  [bg_sync] 游戏感 132 BPM 断奏')
    make_bg_v2('bg_sync.wav', bpm=132,
               pattern=['C', 'G', 'A', '.', 'G', 'E', 'D', 'G'], key=SCALE,
               bars=4, note_len=0.5, drone_f=65.41,
               staccato=True, swing=True, drone_amp=0.12)

    print('完成。')
