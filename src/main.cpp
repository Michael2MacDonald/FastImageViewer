#include <libraw/libraw.h>
#include <iostream>

#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QImage>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle("FastImageViewer");
    window.resize(800, 600);

    // Basic libraw usage example
    LibRaw rawProcessor;
    int ret = rawProcessor.open_file("example.raw"); // Replace with actual file
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw open_file error: " << libraw_strerror(ret) << std::endl;
    } else {
        std::cout << "RAW file loaded successfully." << std::endl;
        // Simple demo: process and show as QImage
        rawProcessor.unpack();
        rawProcessor.raw2image();
        // For demo, just show a blank label (actual conversion to QImage is more involved)
    }
    QLabel* label = new QLabel(&window);
    label->setText("RAW image loaded (display not implemented)");
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);
    window.show();
    return app.exec();
}
    return 0;
