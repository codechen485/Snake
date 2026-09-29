#include "snakewindow.h"

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QKeyEvent>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QRandomGenerator>
#include <QSettings>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>

namespace {
// 配色
const QColor kBoard(26, 32, 44);
const QColor kGridLine(34, 42, 56);
const QColor kSnakeHead(88, 214, 141);   // 蛇头（亮绿）
const QColor kSnakeBody(46, 139, 87);    // 蛇身（管状身体的纯色）
const QColor kSnakeEdge(20, 60, 35);     // 蛇的描边：跟背景对比度 7:1，一描就浮起来
const QColor kFood(231, 76, 60);
const QColor kWall(96, 110, 132);      // 障碍物本体：冷灰蓝，跟绿蛇/红果区分明显
const QColor kWallEdge(48, 56, 72);    // 障碍物描边，比本体深，画出来有厚度
const QColor kOverlay(0, 0, 0, 150);

// ---- 顶部得分 / 速度 ----
// 文字画在背景图 ground.png 顶端那条深绿色带里，位置不能用 HUD_H 算：
// 背景图是被拉伸后铺满整个窗口的（608 高 -> 640），所以深色带的实际
// 高度 = HUD_H * (图片高 / 棋盘高)，文字要按这个拉伸后的高度居中。
// 文字用白色：深绿底(#49732C)配白字对比度 5.6:1 合格；
const QColor kHudText(255, 255, 255);

// 文字左边界：躲开背景图左上角那个苹果图案
constexpr int HUD_TEXT_LEFT = 72;

QColor lighten(const QColor &c, int amount)
{
    return c.lighter(amount);
}


QString formatDuration(int ms)
{
    const int totalSec = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
}


struct DiffConfig { int baseMs; int accel; int minMs; int foodCount; int wallCount; };
constexpr DiffConfig kDiff[3] = {
    { 260, 3, 120, 3,  0 },
    { 200, 4,  80, 2,  0 },
    { 150, 6,  65, 2,  6 },
};

QString diffNameCn(int d)
{
    static const QString n[3] = { QStringLiteral("简单"),
                                  QStringLiteral("普通"),
                                  QStringLiteral("困难") };
    return n[d];
}

// ini 里区分三档最高分的 key 后缀
QString diffKey(int d)
{
    static const QString k[3] = { QStringLiteral("Easy"),
                                  QStringLiteral("Normal"),
                                  QStringLiteral("Hard") };
    return k[d];
}
} // namespace

SnakeWindow::SnakeWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_timer(new QTimer(this))
{
    setFixedSize(GRID_COLS * CELL, BOARD_ORIGIN_Y + GRID_ROWS * CELL);
    setWindowTitle(QStringLiteral("贪吃蛇"));


    setWindowIcon(QIcon(QStringLiteral(":/src/icon2.ico")));

    // 方向键默认会被 Qt 拿去在控件之间切换焦点，这里必须关掉，
    // 否则按一下方向键焦点就跑了，keyPressEvent 根本收不到。
    setFocusPolicy(Qt::StrongFocus);

    connect(m_timer, &QTimer::timeout, this, &SnakeWindow::tick);


    m_bg.load(QStringLiteral(":/src/ground.png"));

    // ---- 吃到食物的音效 ----
    // QSoundEffect 只能播未压缩的 WAV，适合这种短促的效果音：延迟低、能密集触发。
    // 这个音效源文件峰值接近满刻度(99%)，音量压到 0.5 免得过响。
    m_eatSound = new QSoundEffect(this);
    m_eatSound->setSource(QUrl(QStringLiteral("qrc:/src/eat_sound.wav")));
    m_eatSound->setVolume(0.6);

    // ---- 撞死自己的音效 ----
    // 源文件本身就削顶了(峰值 100%)、RMS 是吃食物音效的 3.6 倍，
    // 音量压到 0.35，否则会比其它声音吵一大截。
    m_deathSound = new QSoundEffect(this);
    m_deathSound->setSource(QUrl(QStringLiteral("qrc:/src/death_sound.wav")));
    m_deathSound->setVolume(0.35);

    // ---- 背景音乐 ----
    // 长达 2 分钟，不能用 QSoundEffect（要转成 10MB+ 的 WAV），
    // 改用 QMediaPlayer，它原生支持 MP3，而且能无限循环。
    m_bgOutput = new QAudioOutput(this);
    m_bgOutput->setVolume(0.20);
    m_bgMusic = new QMediaPlayer(this);
    m_bgMusic->setAudioOutput(m_bgOutput);
    m_bgMusic->setSource(QUrl(QStringLiteral("qrc:/src/game_bgm.mp3")));
    m_bgMusic->setLoops(QMediaPlayer::Infinite);

    // 三档最高分先占位，再按当前难度从 ini 读回。
    m_highScores = QVector<int>(3, 0);
    loadHighScore();

    // 不再直接开跑：摆好局面，等着玩家按空格。
    // 蛇和食物都正常画出来，只是定时器没启动，所以不动。
    setupBoard();
    m_state = Ready;
}

SnakeWindow::~SnakeWindow() = default;

// ---------------------------------------------------------------------------
// 玩法
// ---------------------------------------------------------------------------

void SnakeWindow::setupBoard()
{
    m_snake.clear();

    m_snake << QPoint(5, 10) << QPoint(6, 10) << QPoint(7, 10);

    m_dir     = QPoint(1, 0);
    m_nextDir = m_dir;
    m_score   = 0;
    m_playMs    = 0;      // 每局重新计时
    m_newRecord = false;  // 纪录标志也要清，否则会一直挂着"新纪录"

    // 速度曲线和食物数都取自当前难度
    const DiffConfig &cfg = kDiff[m_difficulty];
    m_speedMs   = cfg.baseMs;
    m_foodCount = cfg.foodCount;

    placeWalls();
    placeFood();
    update();
}

void SnakeWindow::startGame()
{
    setupBoard();

    m_state = Running;
    m_timer->start(m_speedMs);
    m_bgMusic->play();
    update();
}

// ←/→ 循环切换难度。除了 Ready 界面，暂停 / 结束时也能切——
// 切档后统一停表、按新难度重摆、回到 Ready 等玩家开始，
void SnakeWindow::cycleDifficulty(int delta)
{
    m_difficulty = Difficulty((m_difficulty + delta + 3) % 3);
    m_highScore  = m_highScores[m_difficulty];
    m_timer->stop();   // 从暂停 / 结束切过来时确保定时器停住
    setupBoard();      // 按新难度重摆蛇 / 食物 / 障碍
    m_state = Ready;   // 统一回到"选难度、等开始"
    update();
}

void SnakeWindow::toReady()
{
    m_timer->stop();
    setupBoard();
    m_state = Ready;
    update();
}

void SnakeWindow::tick()
{
    if (m_state != Running)
        return;

    // 方向在真正走之前才生效：一次按键绝不会把蛇掉头撞死
    m_dir = m_nextDir;

    const QPoint head = wrap(m_snake.last() + m_dir);

    // 撞障碍：和撞自己一样直接结束本局
    if (m_walls.contains(head)) {
        endGame();
        return;
    }

    const bool willEat = m_foods.contains(head);

    // 先记下被吃掉的是哪一个，等下只补这一个，
    // 另一个没吃到的食物保持原地不动
    const QPoint eaten = head;

    // 撞自己：尾巴那一格这帧会让出来，所以吃食物（不变长）时才把它算进去
    const int bodyLen = m_snake.size() - (willEat ? 0 : 1);
    for (int i = 0; i < bodyLen; ++i) {
        if (m_snake.at(i) == head) {
            endGame();
            return;
        }
    }

    m_snake.append(head);
    // 这一格是按【提速前】的速度走完的，先累加当前 m_speedMs；
    // 下面吃到食物才会调快 m_speedMs，影响的是下一格的耗时。
    m_playMs += m_speedMs;
    if (willEat) {
        // 吃到食物：不删尾巴 = 长一节，加分，提速
        if (m_eatSound)
            m_eatSound->play();
        ++m_score;
        const DiffConfig &cfg = kDiff[m_difficulty];
        m_speedMs = qMax(cfg.minMs, cfg.baseMs - m_score * cfg.accel);
        m_timer->start(m_speedMs);
        m_foods.removeOne(eaten);   // 只拿掉被吃的这个
        addFood();                  // 再补一个新的，另一个食物原地不动
    } else {
        m_snake.removeFirst(); // 没吃到就删尾巴，蛇身长度不变
    }

    update();
}


void SnakeWindow::endGame()
{
    m_state = GameOver;
    m_timer->stop();
    if (m_deathSound)
        m_deathSound->play();
    m_bgMusic->pause();


    if (m_score > m_highScore) {
        m_highScore = m_score;
        m_newRecord = true;
        saveHighScore();
    }

    update();
}

bool SnakeWindow::applyDirection(const QPoint &d)
{
    if (m_state != Running)
        return false;                              // 暂停中、结束后都不接受转向
    if (d + m_dir == QPoint(0, 0))
        return false;                              // 不能 180° 掉头，否则下一帧头就撞进自己的脖子

    m_nextDir = d;
    return true;
}

void SnakeWindow::setPaused(bool paused)
{
    if (paused && m_state == Running) {
        m_state = Paused;
        m_timer->stop();
        m_bgMusic->pause();
    } else if (!paused && m_state == Paused) {
        m_state = Running;
        m_timer->start(m_speedMs);
        m_bgMusic->play();
    }
    update();
}

bool SnakeWindow::confirm(const QString &text)
{

    const bool wasRunning = (m_state == Running);
    if (wasRunning)
        setPaused(true);

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("提示"), text,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    // 关掉弹窗后把焦点拿回主窗口，否则方向键可能失灵
    activateWindow();
    setFocus();

    if (ret == QMessageBox::Yes)
        return true;

    if (wasRunning)
        setPaused(false);
    return false;
}

void SnakeWindow::placeFood()
{
    m_foods.clear();
    // 循环加到满为止：addFood() 一次只加一个。
    // 必须检查返回值——没有空格时 addFood() 加不上东西，
    // 只判断 size 会永远等不到，变成死循环。
    while (m_foods.size() < m_foodCount) {
        if (!addFood())
            break;
    }
}

bool SnakeWindow::addFood()
{
    // 只补一个食物，保留场上已有的。
    if (m_foods.size() >= m_foodCount)
        return false;

    // 候选 = 空格 - 蛇身 - 已有食物 - 障碍。
    // 排除食物避免新旧重叠；排除障碍避免食物长在砖块里吃不到。
    QVector<QPoint> free;
    free.reserve(GRID_COLS * GRID_ROWS - m_snake.size());
    for (int y = 0; y < GRID_ROWS; ++y) {
        for (int x = 0; x < GRID_COLS; ++x) {
            const QPoint p(x, y);
            if (!m_snake.contains(p) && !m_foods.contains(p)
                && !m_walls.contains(p))
                free.append(p);
        }
    }

    if (free.isEmpty())   // 整屏都是蛇 = 通关，没地方放了
        return false;

    const int idx = QRandomGenerator::global()->bounded(free.size());
    m_foods.append(free.at(idx));
    return true;
}

QPoint SnakeWindow::wrap(const QPoint &p) const
{

    const int x = (p.x() + GRID_COLS) % GRID_COLS;
    const int y = (p.y() + GRID_ROWS) % GRID_ROWS;
    return QPoint(x, y);
}

void SnakeWindow::placeWalls()
{
    m_walls.clear();
    const int n = kDiff[m_difficulty].wallCount;
    if (n <= 0)
        return;   

    // 候选格排除"蛇起步走廊"：蛇初始在 (5,10)(6,10)(7,10) 朝右，
    // 把 x∈[3,16]、y∈[8,12] 这一片留空，保证开局有几步缓冲不会秒撞。
    QVector<QPoint> free;
    for (int y = 0; y < GRID_ROWS; ++y) {
        for (int x = 0; x < GRID_COLS; ++x) {
            if (x >= 3 && x <= 16 && y >= 8 && y <= 12)
                continue;
            free.append(QPoint(x, y));
        }
    }

    for (int i = 0; i < n && !free.isEmpty(); ++i) {
        const int idx = QRandomGenerator::global()->bounded(free.size());
        m_walls.append(free.at(idx));
        free.removeAt(idx);
    }
}

// 最高分存进用户目录下的 ini（Windows: %APPDATA%\SnakeGame\Snake.ini）。
// 用 IniFormat 而非默认的注册表：跨平台一致，玩家也能直接打开查看/备份。
// 三档分开存，key 形如 highScoreEasy / highScoreNormal / highScoreHard。
void SnakeWindow::loadHighScore()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("SnakeGame"), QStringLiteral("Snake"));
    for (int d = 0; d < 3; ++d)
        m_highScores[d] = s.value(QStringLiteral("highScore") + diffKey(d), 0).toInt();
    // m_highScore 始终跟随当前难度，其余绘制/判定逻辑不用改
    m_highScore = m_highScores[m_difficulty];
}

void SnakeWindow::saveHighScore()
{
    m_highScores[m_difficulty] = m_highScore;
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("SnakeGame"), QStringLiteral("Snake"));
    s.setValue(QStringLiteral("highScore") + diffKey(m_difficulty), m_highScore);
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------

void SnakeWindow::keyPressEvent(QKeyEvent *event)
{
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Up:    handled = applyDirection(QPoint(0, -1)); break;
    case Qt::Key_Down:  handled = applyDirection(QPoint(0,  1)); break;

    case Qt::Key_Left:
        // 只有跑动中左右键才是转向；准备 / 暂停 / 结束时都用来切换难度
        if (m_state == Running) { handled = applyDirection(QPoint(-1, 0)); }
        else                    { cycleDifficulty(-1); handled = true; }
        break;

    case Qt::Key_Right:
        if (m_state == Running) { handled = applyDirection(QPoint( 1, 0)); }
        else                    { cycleDifficulty(+1); handled = true; }
        break;

    case Qt::Key_Space:
        if (m_state == Ready) {
            startGame();
        } else if (m_state == Running) {
            setPaused(true);
        } else if (m_state == Paused) {
            setPaused(false);
        } else {
            toReady();            // 结束后按空格 = 回难度选择界面换难度
        }
        break;

    case Qt::Key_R:
        // 已经结束时直接重开；还在玩就先问一句，免得手抖按到丢掉成绩
        if (m_state == GameOver) {
            startGame();
        } else if (!confirm(QStringLiteral("确定要重新开始吗？"))) {
            handled = false;
        } else {
            startGame();
        }
        break;

    case Qt::Key_Escape:
        if (!confirm(QStringLiteral("是否要退出游戏？")))
            handled = false;
        else
            close();
        break;

    default:
        handled = false;
        break;
    }

    if (!handled) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // 吃掉事件，否则方向键还会被拿去移动焦点
    event->accept();
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void SnakeWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int boardW = GRID_COLS * CELL;
    const int boardH = GRID_ROWS * CELL;

    // 棋盘在窗口里的纵向起点。棋盘整体下移 BOARD_ORIGIN_Y，
    // 让第 0 行不落在背景图顶部的深色带上。
    const int boardTop = BOARD_ORIGIN_Y;

    // ---- 底板：整窗铺满，位置不随棋盘偏移 ----
    if (!m_bg.isNull())
        p.drawPixmap(0, 0, width(), height(), m_bg);
    else
        p.fillRect(0, 0, width(), height(), kBoard);

    // ---- 网格线 ----
    // p.setPen(kGridLine);
    // for (int x = 1; x < GRID_COLS; ++x)
    //     p.drawLine(x * CELL, 0, x * CELL, boardH);
    // for (int y = 1; y < GRID_ROWS; ++y)
    //     p.drawLine(0, y * CELL, boardW, y * CELL);

    // ---- 障碍物（画成方块，跟圆形的食物/蛇区分开）----
    // 先描一层深色底再画内缩本体，看起来有点厚度，不像贴了张色块。
    // Ready（还没开始）时不画：此时页面以难度选择文字为主，
    // 一堆障碍方块会把画面弄得很乱，等真正开跑再显示。
    if (m_state != Ready) {
        p.setPen(Qt::NoPen);
        for (const QPoint &w : m_walls) {
            const QRectF r(w.x() * CELL, w.y() * CELL + boardTop, CELL, CELL);
            p.setBrush(kWallEdge);
            p.drawRect(r);
            p.setBrush(kWall);
            p.drawRect(r.adjusted(2, 2, -2, -2));
        }
    }

    // ---- 食物
    p.setPen(Qt::NoPen);
    for (const QPoint &food : m_foods) {
        const QRectF foodRect(food.x() * CELL + 4,
                              food.y() * CELL + boardTop + 4,
                              CELL - 8, CELL - 8);
        p.setBrush(kFood);
        p.drawEllipse(foodRect);
        p.setBrush(lighten(kFood, 150));
        p.drawEllipse(QRectF(foodRect.x() + 5, foodRect.y() + 5, 6, 6));
    }

    // ---- 蛇：圆润连续的管状身体 + 圆头大眼（卡通贪吃蛇）
    const int segCount = m_snake.size();
    const double bodyW = CELL * 0.92;   // 身体管宽（等宽）
    const double headR = CELL * 0.66;   // 头半径（比身体宽，突出）
    const double edge  = CELL * 0.10;   // 描边每侧宽出的量

    // 格子坐标 -> 像素中心（纵向要加 boardTop 偏移）
    auto centerOf = [&](const QPoint &seg) {
        return QPointF(seg.x() * CELL + CELL / 2.0,
                       seg.y() * CELL + boardTop + CELL / 2.0);
    };

    // 穿墙处相邻两节在网格上不相邻，连线会横穿屏幕，必须断开
    auto isWrapped = [&](int i) {
        const QPoint d = m_snake.at(i + 1) - m_snake.at(i);
        return qAbs(d.x()) > 1 || qAbs(d.y()) > 1;
    };

    // 身体：两轮绘制，先整体描边（深色更粗）再整体本体（纯色），
    // 圆头端点让段与段平滑衔接成一条连续的管，不是一节节的圆珠。
    for (int layer = 0; layer < 2; ++layer) {
        for (int i = 0; i + 1 < segCount; ++i) {
            if (isWrapped(i))
                continue;
            QPen pen;
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            if (layer == 0) {
                pen.setColor(kSnakeEdge);
                pen.setWidthF(bodyW + edge * 2);
            } else {
                pen.setColor(kSnakeBody);
                pen.setWidthF(bodyW);
            }
            p.setPen(pen);
            p.drawLine(centerOf(m_snake.at(i)), centerOf(m_snake.at(i + 1)));
        }
    }

    // 头：突出的圆 + 两只大卡通眼睛，朝向跟着 m_dir 转
    if (segCount > 0) {
        const QPointF hc = centerOf(m_snake.last());
        const double dx = m_dir.x(), dy = m_dir.y();
        const double px = -dy, py = dx;   // 朝向的垂直方向 = 两眼连线方向

        p.setPen(Qt::NoPen);
        p.setBrush(kSnakeEdge);
        p.drawEllipse(hc, headR + edge, headR + edge);
        p.setBrush(kSnakeHead);
        p.drawEllipse(hc, headR, headR);

        // 大眼睛：白眼 + 黑瞳，并排在头的前部，占头部较大比例（卡通感）
        const double fwd  = headR * 0.30;   // 往前偏
        const double side = headR * 0.42;   // 左右分列（两眼半间距）
        const double er   = headR * 0.34;   // 眼白半径（调小一点）
        for (int s = -1; s <= 1; s += 2) {
            const QPointF eye(hc.x() + dx * fwd + px * side * s,
                              hc.y() + dy * fwd + py * side * s);
            p.setBrush(Qt::white);
            p.drawEllipse(eye, er, er);
            // 瞳孔朝前一点，像在盯着前进方向
            p.setBrush(QColor(30, 40, 35));
            p.drawEllipse(eye + QPointF(dx, dy) * (er * 0.22), er * 0.52, er * 0.52);
        }
    }

    // ---- 顶部得分 / 速度（印在背景图顶端那条深绿色带里）----
    QFont f = p.font();
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);

    // 背景图铺满整个窗口，图片里顶端深色带占 95/608 的高度，
    // 拉伸到窗口后的实际高度按同一比例换算——写死数值会随窗口尺寸变化而错位。
    const int hudTopH = m_bg.isNull()
                            ? HUD_H
                            : int(height() * 95.0 / 608.0);
    const QRect hudRect(HUD_TEXT_LEFT, 0, width() - HUD_TEXT_LEFT * 2, hudTopH);

    p.setPen(kHudText);
    p.drawText(hudRect, Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("得分 %1").arg(m_score));
    // 中间常驻最高分，玩家随时看得到还差多少破纪录
    p.drawText(hudRect, Qt::AlignVCenter | Qt::AlignHCenter,
               QStringLiteral("最高 %1").arg(m_highScore));
    p.drawText(hudRect, Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("速度 %1").arg(1000 / m_speedMs));

    // ---- 开始 / 暂停 / 结束 遮罩 ----
    const QRect boardRect(0, boardTop, boardW, boardH);

    if (m_state == Ready) {
        // 开始界面【不压暗】：只留难度选择文字，障碍物此时不画（见上面 m_state != Ready），
        // 免得方块把画面弄乱。浅绿背景上白字配深绿描边才清楚，抽成 lambda 复用。
        auto drawOutlined = [&](const QRect &r, const QString &text, int pt, bool bold) {
            QFont ft = p.font();
            ft.setPointSize(pt);
            ft.setBold(bold);
            p.setFont(ft);
            p.setPen(QColor(20, 60, 35));            // 深绿描边
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy)
                        p.drawText(r.translated(dx * 2, dy * 2), Qt::AlignCenter, text);
            p.setPen(Qt::white);                     // 白字
            p.drawText(r, Qt::AlignCenter, text);
        };

        const int cx = boardRect.center().x();

        // 标题
        drawOutlined(QRect(cx - 150, boardTop + 130, 300, 40),
                     QStringLiteral("选 择 难 度"), 20, true);
        // 当前难度（左右箭头提示可切换）
        drawOutlined(QRect(cx - 150, boardTop + 178, 300, 44),
                     QStringLiteral("◀　%1　▶").arg(diffNameCn(m_difficulty)), 18, true);

        // 开始提示
        drawOutlined(QRect(cx - 150, boardTop + 238, 300, 44),
                     QStringLiteral("按 空 格 开 始"), 16, true);

        // 操作说明
        QFont tiny = p.font();
        tiny.setPointSize(11);
        tiny.setBold(false);
        p.setFont(tiny);
        p.setPen(QColor(30, 75, 45));
        p.drawText(QRect(cx - 260, boardTop + 293, 520, 24), Qt::AlignCenter,
                   QStringLiteral("←/→ 切换难度　·　方向键移动　·　空格暂停　·　R 重开"));
    } else if (m_state == Paused || m_state == GameOver) {
        p.fillRect(rect(), kOverlay);

        QFont big = p.font();
        big.setPointSize(22);
        big.setBold(true);
        p.setFont(big);
        p.setPen(Qt::white);

        if (m_state == Paused) {
            p.drawText(boardRect, Qt::AlignCenter,
                       QStringLiteral("已暂停\n空格继续　·　←/→ 换难度"));
        } else {
            
            p.drawText(QRect(0, boardTop + 55, boardW, 50), Qt::AlignCenter,
                       QStringLiteral("游戏结束"));

            // 破纪录提示：金色，只在真的刷新最高分时出现
            if (m_newRecord) {
                QFont rec = p.font();
                rec.setPointSize(14);
                rec.setBold(true);
                p.setFont(rec);
                p.setPen(QColor(255, 200, 60));
                p.drawText(QRect(0, boardTop + 108, boardW, 34), Qt::AlignCenter,
                           QStringLiteral("★ 新纪录！"));
            }

            // 统计明细
            QFont mid = p.font();
            mid.setPointSize(12);
            mid.setBold(false);
            p.setFont(mid);
            p.setPen(kHudText);
            const QString stats = QStringLiteral(
                                      "本局得分    %1\n"
                                      "最高纪录    %2\n"
                                      "存活时长    %3\n"
                                      "游戏难度    %4\n\n"
                                      "R 重开本难度 · ←/→ 换难度")
                                      .arg(m_score)
                                      .arg(m_highScore)
                                      .arg(formatDuration(m_playMs))
                                      .arg(diffNameCn(m_difficulty));
            p.drawText(QRect(0, boardTop + 148, boardW, 190), Qt::AlignCenter, stats);
        }
    }
}
