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


// ...existing code...
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
#include <QPushButton>
#include <QIcon>
#include <QThreadPool>
#include <QRunnable>
#include <QSplitter>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>

class PreviewBarScrollArea : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (event->angleDelta().y() != 0) {
            int numDegrees = event->angleDelta().y();
            int numSteps = numDegrees / 8;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - numSteps);
            event->accept();
        } else {
            QScrollArea::wheelEvent(event);
        }
    }
};

class ImageViewerWindow : public QMainWindow {
    std::atomic<uint64_t> imageRequestToken{0};
    int thumbnailThreadCount = 4; // Configurable number of threads for thumbnails
    QThreadPool* thumbnailThreadPool = nullptr;
    QWidget* previewBarWidget = nullptr;
    QHBoxLayout* previewBarLayout = nullptr;
    QScrollArea* previewBarScrollArea = nullptr;

    QFutureWatcher<QPixmap>* imageLoadWatcher = nullptr;
    QLabel* metadataLabel = nullptr;
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
        thumbnailThreadPool = new QThreadPool(this);
        thumbnailThreadPool->setMaxThreadCount(thumbnailThreadCount);

        // --- Splitter for main image and metadata ---
        QSplitter* hSplitter = new QSplitter(Qt::Horizontal, this);

    imageviewLabel = new QLabel;
    imageviewLabel->setAlignment(Qt::AlignCenter);
    imageviewLabel->setScaledContents(false);

    imageScrollArea = new QScrollArea;
    imageScrollArea->setWidget(imageviewLabel);
    imageScrollArea->setWidgetResizable(true);
    imageScrollArea->setAlignment(Qt::AlignCenter);
    // Add event filter to update image on resize
    imageScrollArea->installEventFilter(this);

        hSplitter->addWidget(imageScrollArea);

        // Setup metadata pane and store label pointer
        QVBoxLayout* metaLayout = qobject_cast<QVBoxLayout*>(metadataWidget->layout());
        if (!metaLayout) metaLayout = new QVBoxLayout(metadataWidget);
        metadataLabel = new QLabel("Metadata Sidebar");
        metadataLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        metaLayout->addWidget(metadataLabel);
        hSplitter->addWidget(metadataWidget);
        hSplitter->setStretchFactor(0, 1);
        hSplitter->setStretchFactor(1, 0);

        // --- Splitter for preview bar and main content ---
        QSplitter* vSplitter = new QSplitter(Qt::Vertical, this);
        vSplitter->addWidget(hSplitter);

    previewBarWidget = new QWidget;
    previewBarLayout = new QHBoxLayout(previewBarWidget);
    previewBarLayout->setSpacing(4);
    previewBarLayout->setContentsMargins(4, 4, 4, 4);
    previewBarScrollArea = new PreviewBarScrollArea;
    previewBarScrollArea->setWidget(previewBarWidget);
    previewBarScrollArea->setWidgetResizable(true);
    previewBarScrollArea->setMinimumHeight(40);
    previewBarScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    previewBarScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Add event filter to update thumbnails on resize
    previewBarScrollArea->installEventFilter(this);
    vSplitter->addWidget(previewBarScrollArea);
    vSplitter->setStretchFactor(0, 1);
    vSplitter->setStretchFactor(1, 0);

        setCentralWidget(vSplitter);

        QToolBar* toolbar = new QToolBar("Main Toolbar", this);
        QAction* openAction = toolbar->addAction("Open");
        QAction* openDirAction = toolbar->addAction("Open Directory");
        toolbar->addAction("Save");
        toolbar->addAction("Zoom In");
        toolbar->addAction("Zoom Out");
        addToolBar(Qt::TopToolBarArea, toolbar);

        setFocusPolicy(Qt::StrongFocus);
        qApp->installEventFilter(this);
        updateImage();

        connect(openAction, &QAction::triggered, this, &ImageViewerWindow::openImage);
        connect(openDirAction, &QAction::triggered, this, &ImageViewerWindow::openDirectory);
    }

private slots:
    void openImage() {
        QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(), "RAW Images (*.arw *.cr2 *.nef *.dng *.rw2 *.orf *.raf *.srw *.pef *.raw);;All Files (*)");
        if (fileName.isEmpty()) return;

        // Clear thumbnail bar and image list
        imageFileList.clear();
        currentImageIndex = -1;
        // Remove all thumbnail widgets
        QLayoutItem* child;
        while ((child = previewBarLayout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }

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
        updatePreviewBar();
    }
    void updatePreviewBar() {
        // Clear previous thumbnails
        QLayoutItem* child;
        while ((child = previewBarLayout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
            int thumbCount = imageFileList.size();
            int minWidth = thumbCount * 84 + 8; // 80px thumb + 4px spacing + margin
            previewBarWidget->setMinimumWidth(minWidth);
            // Add thumbnails for each image (always half-size RAW preview)
            for (int i = 0; i < thumbCount; ++i) {
                QString path = imageFileList[i];
                QPushButton* thumbBtn = new QPushButton;
                thumbBtn->setFixedSize(80, 80);
                thumbBtn->setStyleSheet("border: none;");
                // thumbBtn->setToolTip(path);
                // Load thumbnail in background using thread pool
                class ThumbTask : public QRunnable {
                public:
                    ThumbTask(QPushButton* btn, QString p) : thumbBtn(btn), path(p) {}
                    void run() override {
                        LibRaw rawProcessor;
                        QByteArray fileNameArray = path.toLocal8Bit();
                        const char* filenameC = fileNameArray.constData();
                        int ret = rawProcessor.open_file(filenameC);
                        if (ret == LIBRAW_SUCCESS) {
                            rawProcessor.imgdata.params.half_size = 1; // Always half-size for thumbnail
                            rawProcessor.imgdata.params.use_auto_wb = 1;
                            ret = rawProcessor.unpack();
                            if (ret == LIBRAW_SUCCESS) {
                                ret = rawProcessor.dcraw_process();
                                if (ret == LIBRAW_SUCCESS) {
                                    libraw_processed_image_t* img = rawProcessor.dcraw_make_mem_image();
                                    if (img && img->type == LIBRAW_IMAGE_BITMAP) {
                                        QImage qimg(img->data, img->width, img->height, img->colors * img->width, QImage::Format_RGB888);
                                        QPixmap pixmap = QPixmap::fromImage(qimg.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                                        QMetaObject::invokeMethod(thumbBtn, [btn=thumbBtn, pixmap]() {
                                            btn->setIcon(QIcon(pixmap));
                                            btn->setIconSize(QSize(76, 76));
                                        }, Qt::QueuedConnection);
                                    }
                                    LibRaw::dcraw_clear_mem(img);
                                }
                            }
                        }
                    }
                private:
                    QPushButton* thumbBtn;
                    QString path;
                };
                thumbnailThreadPool->start(new ThumbTask(thumbBtn, path));
                connect(thumbBtn, &QPushButton::clicked, this, [this, i]() {
                    currentImageIndex = i;
                    loadCurrentImage();
                });
                previewBarLayout->addWidget(thumbBtn);
        }
        previewBarWidget->update();
    }

    void loadCurrentImage() {
    if (imageFileList.isEmpty() || currentImageIndex < 0 || currentImageIndex >= imageFileList.size()) return;
    showLoadingIndicator();
    // Do not call updateImage() here; loadImageFromPath will update when ready
    loadImageFromPath(imageFileList[currentImageIndex]);
    }

    void loadImageFromPath(const QString& filePath) {
        // Cancel any previous image loading
        if (imageLoadWatcher && imageLoadWatcher->isRunning()) {
            imageLoadWatcher->cancel();
        }
        // Generate a new request token
        uint64_t thisRequest = ++imageRequestToken;
        // Highlight thumbnail immediately
        updateImage();
        // Show loading indicator in main image area
        showLoadingIndicator();
        // Threaded image and metadata extraction
    (void)QtConcurrent::run([this, filePath, thisRequest]() {
            QPixmap loadedPixmap;
            struct MetaCopy {
                char make[64] = {0};
                char model[64] = {0};
                char artist[64] = {0};
                float iso_speed = 0;
                float shutter = 0;
                float aperture = 0;
                float focal_len = 0;
                int width = 0;
                int height = 0;
            } meta;
            LibRaw rawProcessor;
            QByteArray fileNameArray = filePath.toLocal8Bit();
            const char* filenameC = fileNameArray.constData();
            if (access(filenameC, F_OK) != -1) {
                int ret = rawProcessor.open_file(filenameC);
                if (ret == LIBRAW_SUCCESS) {
                    rawProcessor.imgdata.params.half_size = 0;
                    rawProcessor.imgdata.params.use_auto_wb = 1;
                    ret = rawProcessor.unpack();
                    if (ret == LIBRAW_SUCCESS) {
                        ret = rawProcessor.dcraw_process();
                        if (ret == LIBRAW_SUCCESS) {
                            libraw_processed_image_t *img = nullptr;
                            try {
                                img = rawProcessor.dcraw_make_mem_image();
                            } catch (...) {}
                            if (img && img->type == LIBRAW_IMAGE_BITMAP) {
                                QImage::Format format = (img->bits == 16) ? QImage::Format_RGB16 : QImage::Format_RGB888;
                                QImage qimg(img->data, img->width, img->height, format);
                                loadedPixmap = QPixmap::fromImage(qimg);
                            }
                            LibRaw::dcraw_clear_mem(img);
                            strncpy(meta.make, rawProcessor.imgdata.idata.make, sizeof(meta.make)-1);
                            strncpy(meta.model, rawProcessor.imgdata.idata.model, sizeof(meta.model)-1);
                            strncpy(meta.artist, rawProcessor.imgdata.other.artist, sizeof(meta.artist)-1);
                            meta.iso_speed = rawProcessor.imgdata.other.iso_speed;
                            meta.shutter = rawProcessor.imgdata.other.shutter;
                            meta.aperture = rawProcessor.imgdata.other.aperture;
                            meta.focal_len = rawProcessor.imgdata.other.focal_len;
                            meta.width = rawProcessor.imgdata.sizes.width;
                            meta.height = rawProcessor.imgdata.sizes.height;
                        }
                    }
                }
            }
            // Update UI in main thread
            QMetaObject::invokeMethod(this, [this, filePath, meta, loadedPixmap, thisRequest]() {
                if (thisRequest != imageRequestToken.load()) return;
                // Update image
                if (!loadedPixmap.isNull()) {
                    originalPixmap = loadedPixmap;
                    updateImage();
                } else {
                    imageviewLabel->setText("<div align='center'><span style='font-size:18px;color:red;'>Failed to load image.</span></div>");
                }
                // Update metadata
                QStringList lines;
                QFileInfo fi(filePath);
                lines << QString("<b>File:</b> %1").arg(fi.fileName());
                lines << QString("<b>Camera:</b> %1 %2").arg(meta.make).arg(meta.model);
                if (strlen(meta.artist) > 0)
                    lines << QString("<b>Artist:</b> %1").arg(meta.artist);
                if (meta.iso_speed > 0)
                    lines << QString("<b>ISO:</b> %1").arg(meta.iso_speed);
                if (meta.shutter > 0)
                    lines << QString("<b>Exposure:</b> %1 s").arg(meta.shutter, 0, 'g', 4);
                if (meta.aperture > 0)
                    lines << QString("<b>Aperture:</b> f/%1").arg(meta.aperture, 0, 'g', 2);
                if (meta.focal_len > 0)
                    lines << QString("<b>Focal Length:</b> %1 mm").arg(meta.focal_len, 0, 'g', 2);
                if (meta.width > 0 && meta.height > 0)
                    lines << QString("<b>Dimensions:</b> %1 x %2").arg(meta.width).arg(meta.height);
                if (metadataLabel) {
                    metadataLabel->setText(lines.join("<br>\n"));
                }
            }, Qt::QueuedConnection);
        });
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
        // Handle resize events for image and thumbnail areas
        if (event->type() == QEvent::Resize) {
            if (obj == imageScrollArea) {
                updateImage();
                return false;
            } else if (obj == previewBarScrollArea) {
                // Update thumbnails to fit new size
                for (int i = 0; i < previewBarLayout->count(); ++i) {
                    QWidget* w = previewBarLayout->itemAt(i)->widget();
                    if (w && w->isWidgetType()) {
                        QPushButton* btn = qobject_cast<QPushButton*>(w);
                        if (btn) {
                            btn->setFixedSize(previewBarScrollArea->viewport()->height() - 8, previewBarScrollArea->viewport()->height() - 8);
                        }
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
        // Update blue border and blue tint for selected thumbnail only
        QWidget* selectedThumb = nullptr;
        for (int i = 0; i < previewBarLayout->count(); ++i) {
            QWidget* w = previewBarLayout->itemAt(i)->widget();
            if (w) {
                if (i == currentImageIndex) {
                    w->setStyleSheet("border: 2px solid #0078d7; background-color: #e6f0fa;");
                    selectedThumb = w;
                } else {
                    w->setStyleSheet("border: none; background-color: transparent;");
                }
            }
        }
        // Scroll preview bar to show selected thumbnail
        if (selectedThumb) {
            previewBarScrollArea->ensureWidgetVisible(selectedThumb, 20, 0);
        }
    }

    QLabel* imageviewLabel;
    QScrollArea* imageScrollArea;
    QPixmap originalPixmap;
    QStringList imageFileList;
    int currentImageIndex = -1;
};


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Metadata sidebar (scrollable)
    QWidget* metadataWidget = new QWidget;
    QVBoxLayout* metadataLayout = new QVBoxLayout(metadataWidget);
    // QLabel* metadataLabel = new QLabel("Metadata Sidebar");
    // metadataLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    // metadataLayout->addWidget(metadataLabel);
    QScrollArea* metadataScrollArea = new QScrollArea;
    metadataScrollArea->setWidget(metadataWidget);
    metadataScrollArea->setWidgetResizable(true);
    metadataScrollArea->setMinimumWidth(200);

    QPixmap emptyPixmap;
    ImageViewerWindow window(emptyPixmap, metadataScrollArea);
    window.show();
    return app.exec();
}
