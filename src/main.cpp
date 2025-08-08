#include <stdio.h>
#include <fmt/core.h>
#include <string.h>
#include <math.h>

#include <libraw/libraw.h>

#ifndef LIBRAW_WIN32_CALLS
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif

#ifdef LIBRAW_WIN32_CALLS
#define snprintf _snprintf
#endif


#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QImage>
#include <QToolBar>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QScrollArea>
#include <QResizeEvent>
#include <QScrollBar>
#include <QDir>
#include <QKeyEvent>

class ImageViewerWindow : public QMainWindow {
    void showLoadingIndicator() {
        imageviewLabel->setText("<div align='center'><span style='font-size:32px;'>Loading...</span></div>");
        imageviewLabel->setPixmap(QPixmap());
        imageviewLabel->resize(imageScrollArea->viewport()->size());
        QApplication::processEvents(); // Force UI update
    }
public:
    ImageViewerWindow(const QPixmap& imagePixmap, QWidget* metadataWidget, QWidget* parent = nullptr)
        : QMainWindow(parent), originalPixmap(imagePixmap) {
        setWindowTitle("FastImageViewer");
        resize(800, 600);

        QWidget* centralWidget = new QWidget(this);
        QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

        imageviewLabel = new QLabel;
        imageviewLabel->setAlignment(Qt::AlignCenter);
        imageviewLabel->setScaledContents(false); // Prevent squishing

        imageScrollArea = new QScrollArea;
        imageScrollArea->setWidget(imageviewLabel);
        imageScrollArea->setWidgetResizable(true);
        imageScrollArea->setAlignment(Qt::AlignCenter);

        mainLayout->addWidget(imageScrollArea, 1);
        mainLayout->addWidget(metadataWidget);

        QToolBar* toolbar = new QToolBar("Main Toolbar", this);
        QAction* openAction = toolbar->addAction("Open");
        QAction* openDirAction = toolbar->addAction("Open Directory");
        toolbar->addAction("Save");
        toolbar->addAction("Zoom In");
        toolbar->addAction("Zoom Out");
        addToolBar(Qt::TopToolBarArea, toolbar);

        setCentralWidget(centralWidget);
        setFocusPolicy(Qt::StrongFocus);
        qApp->installEventFilter(this); // Capture key events globally for this window
        updateImage();

        // Connect Open buttons
        connect(openAction, &QAction::triggered, this, &ImageViewerWindow::openImage);
        connect(openDirAction, &QAction::triggered, this, &ImageViewerWindow::openDirectory);
    }

private slots:
    void openImage() {
        QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), "RAW Images (*.arw *.cr2 *.nef *.dng *.rw2 *.orf *.raf *.srw *.pef *.raw);;All Files (*)");
        if (fileName.isEmpty()) return;

        LibRaw rawProcessor;
        QByteArray fileNameArray = fileName.toLocal8Bit();
        const char* filenameC = fileNameArray.constData();
        if (access(filenameC, F_OK) == -1) {
            fmt::print(stderr, "File {} does not exist.\n", filenameC);
            return;
        }
        int ret = rawProcessor.open_file(filenameC);
        if (ret != LIBRAW_SUCCESS) {
            fmt::print(stderr, "LibRaw open_file error: {}\n", libraw_strerror(ret));
            return;
        }
        rawProcessor.imgdata.params.half_size = 0;
        rawProcessor.imgdata.params.use_auto_wb = 1;
        rawProcessor.unpack();
        rawProcessor.dcraw_process();
        libraw_processed_image_t *processed_image = rawProcessor.dcraw_make_mem_image();
        QImage::Format format = (processed_image->bits == 16) ? QImage::Format_RGB16 : QImage::Format_RGB888;
        QImage image(processed_image->data, processed_image->width, processed_image->height, format);
        QPixmap newPixmap = QPixmap::fromImage(image);
        rawProcessor.dcraw_clear_mem(processed_image);
        if (newPixmap.isNull()) {
            fmt::print(stderr, "Failed to create image pixmap\n");
            return;
        }
        originalPixmap = newPixmap;
        updateImage();
    }

    void openDirectory() {
        QString dirName = QFileDialog::getExistingDirectory(this, "Open Directory", QString());
        if (dirName.isEmpty()) return;
        QDir dir(dirName);
        QStringList nameFilters;
        nameFilters << "*.arw" << "*.cr2" << "*.nef" << "*.dng" << "*.rw2" << "*.orf" << "*.raf" << "*.srw" << "*.pef" << "*.raw";
        imageFileList = dir.entryList(nameFilters, QDir::Files, QDir::Name);
        for (int i = 0; i < imageFileList.size(); ++i) {
            imageFileList[i] = dir.absoluteFilePath(imageFileList[i]);
        }
        if (imageFileList.isEmpty()) {
            fmt::print(stderr, "No RAW images found in directory.\n");
            return;
        }
        currentImageIndex = 0;
        loadCurrentImage();
    }

    void loadCurrentImage() {
        if (imageFileList.isEmpty() || currentImageIndex < 0 || currentImageIndex >= imageFileList.size()) return;
        loadImageFromPath(imageFileList[currentImageIndex]);
    }

    void loadImageFromPath(const QString& filePath) {
        showLoadingIndicator();
        LibRaw rawProcessor;
        QByteArray fileNameArray = filePath.toLocal8Bit();
        const char* filenameC = fileNameArray.constData();
        if (access(filenameC, F_OK) == -1) {
            fmt::print(stderr, "File {} does not exist.\n", filenameC);
            return;
        }
        int ret = rawProcessor.open_file(filenameC);
        if (ret != LIBRAW_SUCCESS) {
            fmt::print(stderr, "LibRaw open_file error: {}\n", libraw_strerror(ret));
            return;
        }
        rawProcessor.imgdata.params.half_size = 0;
        rawProcessor.imgdata.params.use_auto_wb = 1;
        rawProcessor.unpack();
        rawProcessor.dcraw_process();
        libraw_processed_image_t *processed_image = rawProcessor.dcraw_make_mem_image();
        QImage::Format format = (processed_image->bits == 16) ? QImage::Format_RGB16 : QImage::Format_RGB888;
        QImage image(processed_image->data, processed_image->width, processed_image->height, format);
        QPixmap newPixmap = QPixmap::fromImage(image);
        rawProcessor.dcraw_clear_mem(processed_image);
        if (newPixmap.isNull()) {
            fmt::print(stderr, "Failed to create image pixmap\n");
            return;
        }
        originalPixmap = newPixmap;
        updateImage();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        updateImage();
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (!imageFileList.isEmpty()) {
                if (keyEvent->key() == Qt::Key_Right) {
                    if (currentImageIndex < imageFileList.size() - 1) {
                        ++currentImageIndex;
                        loadCurrentImage();
                        return true;
                    }
                } else if (keyEvent->key() == Qt::Key_Left) {
                    if (currentImageIndex > 0) {
                        --currentImageIndex;
                        loadCurrentImage();
                        return true;
                    }
                }
            }
        }
        return QMainWindow::eventFilter(obj, event);
    }

private:
    void updateImage() {
        QSize areaSize = imageScrollArea->viewport()->size();
        QPixmap scaled = originalPixmap.scaled(areaSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imageviewLabel->setPixmap(scaled);
        imageviewLabel->resize(scaled.size()); // Ensure label matches pixmap size
    }

    QLabel* imageviewLabel;
    QScrollArea* imageScrollArea;
    QPixmap originalPixmap;
    QStringList imageFileList;
    int currentImageIndex = -1;
};


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    LibRaw rawProcessor;
    char *filename = "/home/michael/Projects/FastImageViewer/test-data/Sony - ILCE-7M4 - 14bit (3_2).ARW";
    if (access(filename, F_OK) == -1) {
        fmt::print(stderr, "File {} does not exist.\n", filename);
        return 1;
    }
    int ret = rawProcessor.open_file(filename);
    if (ret != LIBRAW_SUCCESS) {
        fmt::print(stderr, "LibRaw open_file error: {}\n", libraw_strerror(ret));
        return 1;
    }
    fmt::print("RAW file loaded successfully.\n");
    rawProcessor.imgdata.params.half_size = 0;
    rawProcessor.imgdata.params.use_auto_wb = 1;
    rawProcessor.unpack();
    rawProcessor.dcraw_process();
    libraw_processed_image_t *processed_image = rawProcessor.dcraw_make_mem_image();
    QImage::Format format = (processed_image->bits == 16) ? QImage::Format_RGB16 : QImage::Format_RGB888;
    QImage image(processed_image->data, processed_image->width, processed_image->height, format);
    QPixmap imagePixmap = QPixmap::fromImage(image);
    rawProcessor.dcraw_clear_mem(processed_image);
    if (imagePixmap.isNull()) {
        fmt::print(stderr, "Failed to create image pixmap\n");
        return 1;
    }

    // Metadata sidebar (scrollable)
    QWidget* metadataWidget = new QWidget;
    QVBoxLayout* metadataLayout = new QVBoxLayout(metadataWidget);
    QLabel* metadataLabel = new QLabel("Metadata Sidebar");
    metadataLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    metadataLayout->addWidget(metadataLabel);
    QScrollArea* metadataScrollArea = new QScrollArea;
    metadataScrollArea->setWidget(metadataWidget);
    metadataScrollArea->setWidgetResizable(true);
    metadataScrollArea->setMinimumWidth(200);

    ImageViewerWindow window(imagePixmap, metadataScrollArea);
    window.show();
    return app.exec();
}
