#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QMouseEvent>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QImage>
#include <QMenu>

#include <windows.h>
#include <wincodec.h>
#include <wrl.h>
#include <shlwapi.h>
using namespace Microsoft::WRL;

using Microsoft::WRL::ComPtr;

void Converter::convertFile(const QString &inputPath, const QString &outputPath)
{
    qDebug() << "开始转换文件:" << inputPath;
    qDebug() << "输出路径:" << outputPath;
    
    emit conversionProgress(0);  // 开始转换时发送进度信号
    
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        qDebug() << "Failed to initialize COM";
        emit conversionFinished(inputPath, false);
        return;
    }

    bool success = false;
    try {
        // 创建WIC工厂
        ComPtr<IWICImagingFactory> factory;
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)
        );
        if (FAILED(hr)) throw std::runtime_error("Failed to create WIC factory");

        emit conversionProgress(20);  // 工厂创建成功

        // 创建解码器
        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(
            reinterpret_cast<LPCWSTR>(inputPath.utf16()),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
        );
        if (FAILED(hr)) throw std::runtime_error("Failed to create decoder");

        emit conversionProgress(40);  // 解码器创建成功

        // 获取图像帧
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) throw std::runtime_error("Failed to get frame");

        emit conversionProgress(60);  // 帧获取成功

        // 创建编码器
        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory->CreateEncoder(
            GUID_ContainerFormatJpeg,
            nullptr,
            &encoder
        );
        if (FAILED(hr)) throw std::runtime_error("Failed to create encoder");

        emit conversionProgress(80);  // 编码器创建成功

        // 创建输出文件
        ComPtr<IStream> stream;
        hr = SHCreateStreamOnFileW(
            reinterpret_cast<LPCWSTR>(outputPath.utf16()),
            STGM_CREATE | STGM_WRITE,
            &stream
        );
        if (FAILED(hr)) throw std::runtime_error("Failed to create output file");

        // 初始化编码器
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) throw std::runtime_error("Failed to initialize encoder");

        // 创建新帧
        ComPtr<IWICBitmapFrameEncode> frameEncode;
        hr = encoder->CreateNewFrame(&frameEncode, nullptr);
        if (FAILED(hr)) throw std::runtime_error("Failed to create frame");

        // 初始化帧
        hr = frameEncode->Initialize(nullptr);
        if (FAILED(hr)) throw std::runtime_error("Failed to initialize frame");

        // 设置帧大小
        UINT width, height;
        hr = frame->GetSize(&width, &height);
        if (FAILED(hr)) throw std::runtime_error("Failed to get size");

        hr = frameEncode->SetSize(width, height);
        if (FAILED(hr)) throw std::runtime_error("Failed to set size");

        // 设置像素格式
        WICPixelFormatGUID pixelFormat;
        hr = frame->GetPixelFormat(&pixelFormat);
        if (FAILED(hr)) throw std::runtime_error("Failed to get pixel format");

        hr = frameEncode->SetPixelFormat(&pixelFormat);
        if (FAILED(hr)) throw std::runtime_error("Failed to set pixel format");

        // 从源复制像素数据
        hr = frameEncode->WriteSource(frame.Get(), nullptr);
        if (FAILED(hr)) throw std::runtime_error("Failed to write pixels");

        // 提交帧
        hr = frameEncode->Commit();
        if (FAILED(hr)) throw std::runtime_error("Failed to commit frame");

        // 提交编码器
        hr = encoder->Commit();
        if (FAILED(hr)) throw std::runtime_error("Failed to commit encoder");

        success = true;
        emit conversionProgress(100);  // 转换完成
    }
    catch (const std::exception& e) {
        qDebug() << "Error converting file:" << e.what();
    }

    CoUninitialize();
    emit conversionFinished(inputPath, success);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_mousePressed(false)
    , pendingConversions(0)
{
    ui->setupUi(this);
    setAcceptDrops(true);
    setWindowFlags(Qt::FramelessWindowHint);
    
    // 设置窗口和中央部件的背景色
    setStyleSheet("QMainWindow { background: white; }");
    ui->centralwidget->setStyleSheet("QWidget { background: white; }");
    
    // 设置内容区域的边距
    ui->contentLayout->setContentsMargins(10, 10, 10, 10);

    // 初始化转换器
    converter = new Converter;
    converter->moveToThread(&converterThread);
    connect(&converterThread, &QThread::finished, converter, &QObject::deleteLater);
    connect(this, &MainWindow::destroyed, &converterThread, &QThread::quit);
    connect(converter, &Converter::conversionFinished, this, &MainWindow::onConversionFinished);
    connect(converter, &Converter::conversionProgress, this, &MainWindow::onConversionProgress);
    converterThread.start();
}

MainWindow::~MainWindow()
{
    converterThread.quit();
    converterThread.wait();
    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        QString filePath = url.toLocalFile();
        QFileInfo fileInfo(filePath);
        if (fileInfo.isFile()) {
            processFile(filePath);
        } else if (fileInfo.isDir()) {
            processFolderFiles(filePath);
        }
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_mousePressed = true;
        m_mousePos = event->globalPos() - pos();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mousePressed && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_mousePos);
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_mousePressed = false;
    }
}

void MainWindow::on_minimizeButton_clicked()
{
    showMinimized();
}

void MainWindow::on_maximizeButton_clicked()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::on_closeButton_clicked()
{
    close();
}

void MainWindow::on_addFileButton_clicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this,
        "选择 HEIC 文件", "", "HEIC 文件 (*.heic)");
    
    for (const QString &file : files) {
        processFile(file);
    }
}

void MainWindow::on_addFolderButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        "选择文件夹", "", QFileDialog::ShowDirsOnly);
    
    if (!dir.isEmpty()) {
        processFolderFiles(dir);
    }
}

void MainWindow::processFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (fileInfo.suffix().toLower() == "heic") {
        if (!fileList.contains(filePath)) {
            fileList.append(filePath);
            ui->fileListWidget->addItem(fileInfo.fileName());
            ui->stackedWidget->setCurrentIndex(1);  // 切换到文件列表页面
        }
    }
}

void MainWindow::processFolderFiles(const QString &folderPath)
{
    QDir dir(folderPath);
    QStringList files = dir.entryList(QStringList() << "*.heic" << "*.HEIC",
        QDir::Files | QDir::NoDotAndDotDot);
    
    for (const QString &file : files) {
        processFile(dir.absoluteFilePath(file));
    }
}

void MainWindow::on_clearButton_clicked()
{
    ui->fileListWidget->clear();
    fileList.clear();
    ui->progressBar->setValue(0);
    ui->statusbar->clearMessage();
    ui->stackedWidget->setCurrentIndex(0);  // 切换回拖放页面
}

void MainWindow::on_startConvertButton_clicked()
{
    if (fileList.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先添加要转换的文件！");
        return;
    }

    ui->startConvertButton->setEnabled(false);
    ui->addFileButton->setEnabled(false);
    ui->addFolderButton->setEnabled(false);
    ui->clearButton->setEnabled(false);
    ui->fileListWidget->setEnabled(false);
    ui->progressBar->setValue(0);
    
    convertFiles();
}

void MainWindow::convertFiles()
{
    QMutexLocker locker(&mutex);
    for (const QString &filePath : fileList) {
        QFileInfo fileInfo(filePath);
        QString outputPath = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".jpg";
        
        pendingConversions++;
        QMetaObject::invokeMethod(converter, "convertFile", Qt::QueuedConnection,
                                Q_ARG(QString, filePath),
                                Q_ARG(QString, outputPath));
    }
}

void MainWindow::onConversionFinished(const QString &filePath, bool success)
{
    QMutexLocker locker(&mutex);
    QFileInfo fileInfo(filePath);
    if (success) {
        ui->statusbar->showMessage(tr("转换成功: %1").arg(fileInfo.fileName()), 2000);
    } else {
        ui->statusbar->showMessage(tr("转换失败: %1").arg(fileInfo.fileName()), 2000);
    }

    pendingConversions--;
    if (pendingConversions == 0) {
        ui->startConvertButton->setEnabled(true);
        ui->addFileButton->setEnabled(true);
        ui->addFolderButton->setEnabled(true);
        ui->clearButton->setEnabled(true);
        ui->fileListWidget->setEnabled(true);
        fileList.clear();
        ui->fileListWidget->clear();
        ui->stackedWidget->setCurrentIndex(0);  // 切换回拖放页面
    }
}

void MainWindow::on_loginButton_clicked()
{
    QMessageBox::information(this, "提示", "此功能尚未完善，敬请期待！");
}

void MainWindow::on_serviceButton_clicked()
{
    QMenu menu(this);
    menu.setStyleSheet(R"(
        QMenu {
            background-color: white;
            border: 1px solid #DDDDDD;
            border-radius: 4px;
            padding: 5px;
        }
        QMenu::item {
            padding: 8px 20px;
            border-radius: 4px;
            color: #333333;
        }
        QMenu::item:selected {
            background-color: #FF8533;
            color: white;
        }
    )");

    QAction *wechatAction = menu.addAction("微信客服");
    QAction *qqAction = menu.addAction("QQ客服: 1798250814");
    
    connect(wechatAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "微信客服", "暂未实现微信客服功能");
    });
    
    connect(qqAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "QQ客服", "您好！欢迎咨询\nQQ客服号码：1798250814");
    });
    
    menu.exec(ui->serviceButton->mapToGlobal(QPoint(0, ui->serviceButton->height())));
}

void MainWindow::onConversionProgress(int value)
{
    ui->progressBar->setValue(value);
}


