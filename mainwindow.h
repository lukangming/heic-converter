#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QThread>
#include <QMutex>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief HEIC图片转换器类
 * 负责将HEIC格式图片转换为JPG格式
 */
class Converter : public QObject
{
    Q_OBJECT
public:
    explicit Converter(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void convertFile(const QString &inputPath, const QString &outputPath);

signals:
    void conversionFinished(const QString &filePath, bool success);
    void conversionProgress(int value);
};

/**
 * @brief 主窗口类
 * 处理用户界面交互和文件转换流程
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // 拖放相关事件处理
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    
    // 窗口拖动相关事件处理
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    // 窗口控制按钮事件
    void on_minimizeButton_clicked();
    void on_maximizeButton_clicked();
    void on_closeButton_clicked();
    
    // 文件操作按钮事件
    void on_addFileButton_clicked();
    void on_addFolderButton_clicked();
    void on_clearButton_clicked();
    void on_startConvertButton_clicked();
    
    // 用户功能按钮事件
    void on_loginButton_clicked();
    void on_serviceButton_clicked();
    
    // 转换进度和结果处理
    void onConversionFinished(const QString &filePath, bool success);
    void onConversionProgress(int value);

private:
    // 文件处理方法
    void processFile(const QString &filePath);
    void processFolderFiles(const QString &folderPath);
    void convertFiles();

    Ui::MainWindow *ui;          // UI界面
    QThread converterThread;     // 转换线程
    Converter *converter;        // 转换器实例
    QStringList fileList;        // 待转换文件列表
    QMutex mutex;               // 线程同步锁
    bool m_mousePressed;        // 鼠标按下状态
    QPoint m_mousePos;         // 鼠标位置
    int pendingConversions;    // 待转换文件数量
};

#endif // MAINWINDOW_H
