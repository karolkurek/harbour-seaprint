#include "printerworker.h"
#include "papersizes.h"
#include "convertchecker.h"
#include "mimer.h"
#include <QImage>
#include <QMatrix>
#include <QPainter>
#include <QTextDocument>
#include <QPdfWriter>
#include <QAbstractTextDocumentLayout>
#include <QtSvg>
#include "ippprinter.h"
#include "pdf2printable.h"
#include "ppm2pwg.h"
#include "baselinify.h"
#include <fstream>
#include <iostream>

#define OK(call) if(!(call)) throw ConvertFailedException()

PrinterWorker::PrinterWorker(IppPrinter* parent)
{
    _printer = parent;
}

void PrinterWorker::getStrings(QUrl url)
{
    CurlRequester cr(url, CurlRequester::HttpGetRequest);
    awaitResult(cr, "getStringsFinished");
}

void PrinterWorker::getImage(QUrl url)
{
    CurlRequester cr(url, CurlRequester::HttpGetRequest);
    awaitResult(cr, "getImageFinished");
}


void PrinterWorker::getPrinterAttributes(Bytestream msg)
{
    CurlRequester cr(_printer->httpUrl());
    cr.write((char*)msg.raw(), msg.size());
    awaitResult(cr, "getPrinterAttributesFinished");
}

void PrinterWorker::getJobs(Bytestream msg)
{
    CurlRequester cr(_printer->httpUrl());
    cr.write((char*)msg.raw(), msg.size());
    awaitResult(cr, "getJobsRequestFinished");
}

void PrinterWorker::cancelJob(Bytestream msg)
{
    CurlRequester cr(_printer->httpUrl());
    cr.write((char*)msg.raw(), msg.size());
    awaitResult(cr, "cancelJobFinished");
}

void PrinterWorker::identify(Bytestream msg)
{
    CurlRequester cr(_printer->httpUrl());
    cr.write((char*)msg.raw(), msg.size());
    awaitResult(cr, "identifyFinished");
}

void PrinterWorker::print(QString filename, QString mimeType, QString targetFormat, IppMsg job, PrintParameters Params, QMargins margins)
{
    try {
        Mimer* mimer = Mimer::instance();

        Bytestream contents = job.encode();

        emit busyMessage(tr("Preparing"));


        if((mimeType == targetFormat) && (targetFormat == Mimer::Postscript))
        { // Can't process Postscript
            justUpload(filename, contents);
        }
        else if((mimeType == targetFormat) && (targetFormat == Mimer::Plaintext))
        {
            fixupPlaintext(filename, contents);
        }
        else if((mimeType != Mimer::SVG) && mimer->isImage(mimeType) && mimer->isImage(targetFormat))
        { // Just make sure the image is in the desired format (and jpeg baseline-encoded), don't resize locally
            printImageAsImage(filename, contents, targetFormat);
        }
        else if(Params.format != PrintParameters::Invalid) // Params.format can be trusted
        {
            if(mimeType == Mimer::PDF)
            {
                convertPdf(filename, contents, Params);
            }
            else if(mimeType == Mimer::Plaintext)
            {
                convertPlaintext(filename, contents, Params);
            }
            else if(Mimer::isImage(mimeType))
            {
                convertImage(filename, contents, Params, margins);
            }
            else if(Mimer::isOffice(mimeType))
            {
                convertOfficeDocument(filename, contents, Params);
            }
            else
            {
                emit failed(tr("Cannot convert this file format"));
            }
        }
        else
        {
            emit failed(tr("Cannot convert this file format"));
        }

        return;
    }
    catch(const ConvertFailedException& e)
    {
        emit failed(e.what() == QString("") ? tr("Print error") : e.what());
    }
}

void PrinterWorker::justUpload(QString filename, Bytestream header)
{
    emit busyMessage(tr("Printing"));

    CurlRequester cr(_printer->httpUrl());

    QFile file(filename);
    file.open(QFile::ReadOnly);

    OK(cr.write((char*)header.raw(), header.size()));
    QByteArray tmp = file.readAll();
    OK(cr.write(tmp.data(), tmp.length()));
    file.close();

    awaitResult(cr, "printRequestFinished");
}

void PrinterWorker::printImageAsImage(QString filename, Bytestream header, QString targetFormat)
{
    QString imageFormat = "";
    QStringList supportedImageFormats = {Mimer::JPEG, Mimer::PNG};


    qDebug() << ((IppPrinter*)parent())->_attrs;

    if(targetFormat == Mimer::RBMP)
    {
        // ok
    }
    else if(supportedImageFormats.contains(targetFormat))
    {
        imageFormat = targetFormat.split("/")[1];
    }
    else
    {
        throw ConvertFailedException(tr("Unknown target format"));
    }

    QString mimeType = Mimer::instance()->get_type(filename);
    Bytestream OutBts;

    CurlRequester cr(_printer->httpUrl());

    if(mimeType == Mimer::JPEG && targetFormat == Mimer::JPEG)
    {
        std::ifstream ifs = std::ifstream(filename.toStdString(), std::ios::in | std::ios::binary);
        Bytestream InBts(ifs);

        baselinify(InBts, OutBts);
    }
    else if(targetFormat == Mimer::RBMP)
    {
        QImageReader reader(filename);
        reader.setAutoTransform(true);
        QImage inImage = reader.read();
        QBuffer buf;

        if(inImage.isNull())
        {
            qDebug() << "failed to load";
            throw ConvertFailedException(tr("Failed to load image"));
        }

        // TODO: calculate paper width minus margins
        // (depends on understanding/parsing custom paper sizes)
        int width = 576;
        int height = inImage.height() * ((width*1.0)/inImage.width());
        inImage = inImage.scaled(width, height);
        inImage = inImage.convertToFormat(QImage::Format_Mono);
        inImage = inImage.transformed(QMatrix().scale(1,-1));
        buf.open(QIODevice::ReadWrite);
        inImage.save(&buf, "bmp");
        buf.seek(0);
        OutBts = Bytestream(buf.size());
        buf.read((char*)(OutBts.raw()), buf.size());
    }
    else if(targetFormat == mimeType)
    {
        std::ifstream ifs = std::ifstream(filename.toStdString(), std::ios::in | std::ios::binary);
        OutBts = Bytestream(ifs);
    }
    else
    {
        QImageReader reader(filename);
        reader.setAutoTransform(true);
        QImage inImage = reader.read();
        QBuffer buf;

        if(inImage.isNull())
        {
            qDebug() << "failed to load";
            throw ConvertFailedException(tr("Failed to load image"));
        }

        buf.open(QIODevice::ReadWrite);
        inImage.save(&buf, imageFormat.toStdString().c_str());
        buf.seek(0);
        OutBts = Bytestream(buf.size());
        buf.read((char*)(OutBts.raw()), buf.size());
    }

    emit busyMessage(tr("Printing"));

    OK(cr.write((char*)header.raw(), header.size()));
    OK(cr.write((char*)OutBts.raw(), OutBts.size()));

    awaitResult(cr, "printRequestFinished");
}

void PrinterWorker::fixupPlaintext(QString filename, Bytestream header)
{
    CurlRequester cr(_printer->httpUrl());

    QFile inFile(filename);
    if(!inFile.open(QIODevice::ReadOnly))
    {
        throw ConvertFailedException(tr("Failed to open file"));
    }

    QString allText = inFile.readAll();
    if(allText.startsWith("\f"))
    {
        allText.remove(0, 1);
    }

    if(allText.endsWith("\f"))
    {
        allText.chop(1);
    }
    else if(allText.endsWith("\f\n"))
    {
        allText.chop(2);
    }

    QStringList lines;

    for(QString rnline : allText.split("\r\n"))
    {
        lines.append(rnline.split("\n"));
    }

    QByteArray outData = lines.join("\r\n").toUtf8();

    OK(cr.write((char*)header.raw(), header.size()));
    OK(cr.write(outData.data(), outData.length()));

    awaitResult(cr, "printRequestFinished");
}

void PrinterWorker::convertPdf(QString filename, Bytestream header, PrintParameters Params)
{
    emit busyMessage(tr("Printing"));

    CurlRequester cr(_printer->httpUrl());

    OK(cr.write((char*)header.raw(), header.size()));

    write_fun WriteFun([&cr](unsigned char const* buf, unsigned int len) -> bool
              {
                if(len == 0)
                    return true;
                return cr.write((const char*)buf, len);
              });

    progress_fun ProgressFun([this](size_t page, size_t total) -> void
              {
                emit progress(page, total);
              });

    bool verbose = QLoggingCategory::defaultCategory()->isDebugEnabled();

    int res = pdf_to_printable(filename.toStdString(), WriteFun, Params, ProgressFun, verbose);

    if(res != 0)
    {
        throw ConvertFailedException(tr("Conversion failed"));
    }

    awaitResult(cr, "printRequestFinished");
    qDebug() << "Finished";
}

void PrinterWorker::convertImage(QString filename, Bytestream header, PrintParameters Params, QMargins margins)
{
    QString mimeType = Mimer::instance()->get_type(filename);

    if(Params.format == PrintParameters::URF && (Params.hwResW != Params.hwResH))
    { // URF only supports symmetric resolutions
        qDebug() << "Unsupported URF resolution";
        throw ConvertFailedException(tr("Unsupported resolution (dpi)"));
    }

    qDebug() << "Size is" << Params.getPaperSizeWInPixels() << "x" << Params.getPaperSizeHInPixels();

    int leftMarginPx = (margins.left()/2540.0)*Params.hwResW;
    int rightMarginPx = (margins.right()/2540.0)*Params.hwResW;
    int topMarginPx = (margins.top()/2540.0)*Params.hwResH;
    int bottomMarginPx = (margins.bottom()/2540.0)*Params.hwResH;

    int totalXMarginPx = leftMarginPx+rightMarginPx;
    int totalYMarginPx = topMarginPx+bottomMarginPx;

    size_t targetWidth = Params.getPaperSizeWInPixels()-totalXMarginPx;
    size_t targetHeight = Params.getPaperSizeHInPixels()-totalYMarginPx;

    QImage inImage;
    if(mimeType == Mimer::SVG)
    {
        QSvgRenderer renderer(filename);
        if(!renderer.isValid())
        {
            qDebug() << "failed to load svg";
            throw ConvertFailedException(tr("Failed to load image"));
        }
        QSize defaultSize = renderer.defaultSize();
        QSize targetSize(targetWidth, targetHeight);

        if(defaultSize.width() > defaultSize.height())
        {
            targetSize.transpose();
        }

        QSize initialSize = defaultSize.scaled(targetSize, Qt::KeepAspectRatio);

        inImage = QImage(initialSize, QImage::Format_RGB32);
        inImage.fill(QColor("white"));
        QPainter painter(&inImage);

        renderer.render(&painter);
        painter.end(); // Or else the painter segfaults on destruction if we have messed with the image

        if(inImage.width() > inImage.height())
        {
            inImage = inImage.transformed(QMatrix().rotate(270.0));
        }

    }
    else
    {
        QImageReader reader(filename);
        reader.setAutoTransform(true);
        inImage = reader.read();

        if(inImage.isNull())
        {
            qDebug() << "failed to load";
            throw ConvertFailedException(tr("Failed to load image"));
        }
        if(inImage.width() > inImage.height())
        {
            inImage = inImage.transformed(QMatrix().rotate(270.0));
        }

        inImage = inImage.scaled(targetWidth, targetHeight,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }


    if(Params.format == PrintParameters::PDF || Params.format == PrintParameters::Postscript)
    {
        QTemporaryFile tmpPdfFile;
        tmpPdfFile.open();
        QPdfWriter pdfWriter(tmpPdfFile.fileName());
        pdfWriter.setCreator("SeaPrint " SEAPRINT_VERSION);
        QPageSize pageSize({Params.getPaperSizeWInPoints(), Params.getPaperSizeHInPoints()}, QPageSize::Point);
        pdfWriter.setPageSize(pageSize);
        pdfWriter.setResolution(Params.hwResH);
        QPainter painter(&pdfWriter);
        int xOffset = ((pdfWriter.width()-totalXMarginPx)-inImage.width())/2 + leftMarginPx;
        int yOffset = ((pdfWriter.height()-totalYMarginPx)-inImage.height())/2 + topMarginPx;
        painter.drawImage(xOffset, yOffset, inImage);
        painter.end();

        convertPdf(tmpPdfFile.fileName(), header, Params);

    }
    else
    {
        QImage outImage = QImage(Params.getPaperSizeWInPixels(), Params.getPaperSizeHInPixels(), inImage.format());
        outImage.fill(Qt::white);
        QPainter painter(&outImage);
        int xOffset = ((outImage.width()-totalXMarginPx)-inImage.width())/2 + leftMarginPx;
        int yOffset = ((outImage.height()-totalYMarginPx)-inImage.height())/2 + topMarginPx;
        painter.drawImage(xOffset, yOffset, inImage);
        painter.end();

        QBuffer buf;
        buf.open(QIODevice::ReadWrite);
        Bytestream outBts;


        if(inImage.allGray())
        {
            Params.colors = 1; // No need to waste space/bandwidth...
        }

        outImage.save(&buf, Params.colors==1 ? "pgm" : "ppm");
        buf.seek(0);
        // Skip header - TODO consider reimplementing
        buf.readLine(255);
        buf.readLine(255);
        buf.readLine(255);

        Bytestream inBts(Params.getPaperSizeWInPixels() * Params.getPaperSizeHInPixels() * Params.colors);

        if((((size_t)buf.size())-buf.pos()) != inBts.size())
        {
            qDebug() << buf.size() << buf.pos() << inBts.size();
            throw ConvertFailedException();
        }

        buf.read((char*)(inBts.raw()), inBts.size());

        outBts << (Params.format == PrintParameters::URF ? make_urf_file_hdr(1) : make_pwg_file_hdr());

        bool verbose = QLoggingCategory::defaultCategory()->isDebugEnabled();

        bmp_to_pwg(inBts, outBts, 1, Params, verbose);

        CurlRequester cr(_printer->httpUrl());

        emit busyMessage(tr("Printing"));

        OK(cr.write((char*)header.raw(), header.size()));
        OK(cr.write((char*)(outBts.raw()), outBts.size()));

        awaitResult(cr, "printRequestFinished");
    }

    qDebug() << "posted";
}

void PrinterWorker::convertOfficeDocument(QString filename, Bytestream header, PrintParameters Params)
{
    if(Params.format == PrintParameters::URF && (Params.hwResW != Params.hwResH))
    { // URF only supports symmetric resolutions
        qDebug() << "Unsupported URF resolution";
        throw ConvertFailedException(tr("Unsupported resolution (dpi)"));
    }

    QString ShortPaperSize;
    if(CalligraPaperSizes.contains(Params.paperSizeName.c_str()))
    {
        ShortPaperSize = CalligraPaperSizes[Params.paperSizeName.c_str()];
    }
    else
    {
        qDebug() << "Unsupported PDF paper size" << Params.paperSizeName.c_str();
        throw ConvertFailedException(tr("Unsupported PDF paper size"));
    }

    QProcess CalligraConverter(this);
    CalligraConverter.setProgram("calligraconverter");
    QStringList CalligraConverterArgs = {"--batch", "--mimetype", Mimer::PDF, "--print-orientation", "Portrait", "--print-papersize", ShortPaperSize};

    CalligraConverterArgs << filename;

    QTemporaryFile tmpPdfFile;
    tmpPdfFile.open();
    CalligraConverterArgs << tmpPdfFile.fileName();

    qDebug() << "CalligraConverteArgs is" << CalligraConverterArgs;
    CalligraConverter.setArguments(CalligraConverterArgs);

    CalligraConverter.start();

    qDebug() << "CalligraConverter Starting";

    if(!CalligraConverter.waitForStarted())
    {
        qDebug() << "CalligraConverter died";
        throw ConvertFailedException();
    }

    qDebug() << "CalligraConverter Started";

    if(!CalligraConverter.waitForFinished(-1))
    {
        qDebug() << "CalligraConverter failed";
        throw ConvertFailedException();
    }

//    qDebug() << CalligraConverter->readAllStandardError();

    convertPdf(tmpPdfFile.fileName(), header, Params);

    qDebug() << "posted";
}

void PrinterWorker::convertPlaintext(QString filename, Bytestream header, PrintParameters Params)
{
    if(!PaperSizes.contains(Params.paperSizeName.c_str()))
    {
        qDebug() << "Unsupported paper size" << Params.paperSizeName.c_str();
        throw ConvertFailedException(tr("Unsupported paper size"));
    }
    QSizeF size = PaperSizes[Params.paperSizeName.c_str()];

    QFile inFile(filename);
    if(!inFile.open(QIODevice::ReadOnly))
    {
        throw ConvertFailedException(tr("Failed to open file"));
    }

    quint32 resolution = std::min(Params.hwResW, Params.hwResH);

    QTemporaryFile tmpPdfFile;
    tmpPdfFile.open();

    QPdfWriter pdfWriter(tmpPdfFile.fileName());
    pdfWriter.setCreator("SeaPrint " SEAPRINT_VERSION);
    QPageSize pageSize(size, QPageSize::Millimeter);
    pdfWriter.setPageSize(pageSize);
    pdfWriter.setResolution(resolution);

    qreal docHeight = pageSize.sizePixels(resolution).height();

    QTextDocument doc;

    QFont font = QFont("Courier");
    font.setPointSizeF(1);

    qreal charHeight = 0;

    // Find the optimal font size
    while(true) {
        QFont tmpFont = font;
        tmpFont.setPointSizeF(font.pointSizeF()+0.5);
        QFontMetricsF qfm(tmpFont, &pdfWriter);

        charHeight = qfm.lineSpacing();

        if(charHeight*66 > docHeight)
        {
            break;
        }
        font=tmpFont;
    }

    QFontMetricsF qfm(font, &pdfWriter);

    charHeight = qfm.height();

    int textHeight = 60*charHeight;
    qreal margin = ((docHeight-textHeight)/2);
    qreal mmMargin = margin/(resolution/25.4);

    doc.setDefaultFont(font);
    (void)doc.documentLayout(); // wat


    // Needs to be before painter
    pdfWriter.setMargins({mmMargin, mmMargin, mmMargin, mmMargin});

    QPainter painter(&pdfWriter);

    doc.documentLayout()->setPaintDevice(painter.device());
    doc.setDocumentMargin(margin);

    // Hack to make the document and pdfWriter margins overlap
    // Apparently calls to painter.translate() stack... who knew!
    painter.translate(-margin, -margin);

    QRectF body = pageSize.rectPixels(resolution);
    doc.setPageSize(body.size());

    QString allText = inFile.readAll();
    if(allText.startsWith("\f"))
    {
        allText.remove(0, 1);
    }

    if(allText.endsWith("\f"))
    {
        allText.chop(1);
    }
    else if(allText.endsWith("\f\n"))
    {
        allText.chop(2);
    }

    QStringList pages = allText.split('\f');

    bool first = true;
    int pageCount = 0;

    for(QString page : pages)
    {
        if(!first)
        {
            pdfWriter.newPage();
        }
        first = false;

        if(page.endsWith("\n"))
        {
            page.chop(1);
        }
        doc.setPlainText(page);

        int p = 0; // Page number in this document, starting from 0

        while(true)
        {
            painter.save();
            painter.translate(body.left(), body.top() - p*body.height());
            QRectF view(0, p*body.height(), body.width(), body.height());
            painter.setClipRect(view);

            QAbstractTextDocumentLayout::PaintContext context;
            context.clip = view;
            context.palette.setColor(QPalette::Text, Qt::black);
            doc.documentLayout()->draw(&painter, context);
            painter.restore();

            p++;
            pageCount++;

            if(p >= doc.pageCount())
                break;

            pdfWriter.newPage();
        }
    }

    painter.end();

    convertPdf(tmpPdfFile.fileName(), header, Params);

    qDebug() << "Finished";
    qDebug() << "posted";
}

void PrinterWorker::awaitResult(CurlRequester& cr, QString callback)
{
    Bytestream resMsg;
    CURLcode res = cr.await(&resMsg);
    QMetaObject::invokeMethod(_printer, callback.toStdString().c_str(), Qt::QueuedConnection,
                              Q_ARG(CURLcode, res),
                              Q_ARG(Bytestream, resMsg));
}
