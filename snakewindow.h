#ifndef SNAKEWINDOW_H
#define SNAKEWINDOW_H

#include <QMainWindow>
#include <QPixmap>
#include <QPoint>
#include <QVector>

class QTimer;
class QSoundEffect;
class QMediaPlayer;
class QAudioOutput;

class SnakeWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SnakeWindow(QWidget *parent = nullptr);
    ~SnakeWindow() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // ---------- 棋盘常量 ----------
    
    static constexpr int GRID_COLS = 30;   // 横向格子数
    static constexpr int GRID_ROWS = 22;   // 纵向格子数
    static constexpr int CELL      = 20;   // 每格像素
    static constexpr int HUD_H     = 40;   // 顶部得分/速度文字区的高度

    // ---------- 棋盘在窗口里的绘制位置 ----------
    // 背景图 ground.png 顶部有一条深色带（原图 95px，拉伸后约 101px），
    // 得分/速度印在那条带里。棋盘必须整体下移，绕开这条带——
    static constexpr int BOARD_ORIGIN_Y = 60;

    // ---------- 游戏状态 ----------

    enum State { Ready, Running, Paused, GameOver };

    // ---------- 难度档位 ----------
    // 速度曲线 / 食物数 / 障碍数都由它决定，具体参数见 .cpp 的 kDiff 配置表。
    enum Difficulty { Easy, Normal, Hard };

    QTimer *m_timer = nullptr;
    QSoundEffect *m_eatSound = nullptr;   
    QSoundEffect *m_deathSound = nullptr; 
    QMediaPlayer *m_bgMusic = nullptr;  
    QAudioOutput *m_bgOutput = nullptr; // QMediaPlayer 必须配一个音频输出
    QPixmap m_bg;                      


    QVector<QPoint> m_snake;   
    QVector<QPoint> m_foods;
    QVector<QPoint> m_walls;   
    QPoint  m_dir;             // 当前移动方向
    QPoint  m_nextDir;         // 按键缓存的方向，下一帧才真正生效
    State   m_state = Running;
    Difficulty m_difficulty = Normal;
    int     m_score = 0;
    int     m_speedMs = 200;
    int     m_foodCount = 2;       // 同时存在的食物数，按难度取
    int     m_highScore  = 0;      
    QVector<int> m_highScores;
    int     m_playMs     = 0;      // 本局存活时长（毫秒），仅 Running 时累加
    bool    m_newRecord  = false;

    // ---------- 玩法 ----------
    void startGame();     
    void setupBoard();
    void tick();                          // 定时器每响一次走一格
    bool applyDirection(const QPoint &d); // 转向，返回是否被采纳
    bool confirm(const QString &text);   
    void setPaused(bool paused);
    void placeFood();   // 清空并重新铺满全部食物
    bool addFood();     
    void placeWalls();
    QPoint wrap(const QPoint &p) const;   // 穿墙：越界后从对面绕回来
    void endGame();
    void cycleDifficulty(int delta);
    void toReady();        
    void loadHighScore();   
    void saveHighScore();
};   

#endif // SNAKEWINDOW_H
