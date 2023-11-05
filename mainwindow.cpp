#include "mainwindow.h"
#include "ui_mainwindow.h"

// MainWindow 생성자 실행
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 소켓 초기화 및 생성
    m_socket = new QTcpSocket(this);
    // 서버 연결 요청
    m_socket->connectToHost(QHostAddress::LocalHost,8080);
    if(m_socket->waitForConnected())
    {
        // 연결 성공시 출력
        ui->statusBar->showMessage("Connected to Server");
    }
    else
    {
        QMessageBox::critical(this,"QTCPClient", QString("The following error occurred: %1.").arg(m_socket->errorString()));
        exit(EXIT_FAILURE);
    }

    // 연결된 socket에 data 수신 시, slot_readSocket 실행
    connect(m_socket, &QTcpSocket::readyRead, this, &MainWindow::slot_readSocket);

    // singal_newMessage 시그널 발생 시, slot_displayMessage 실행
    connect(this, &MainWindow::signal_newMessage,
            this, &MainWindow::slot_displayMessage);

    // 연결된 소켓과 연결이 종료되면, slot_discardSocket 실행
    connect(m_socket, &QTcpSocket::disconnected,
            this,     &MainWindow::slot_discardSocket);

    // 연결된 소켓에 문제가 발생하면, slot_displayError 실행
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this,     &MainWindow::slot_displayError);

}

// [ex.02.2]
MainWindow::~MainWindow()
{
    // 소멸자, socket 해제
    if(m_socket->isOpen())
        m_socket->close();
    delete ui;
}


// 서버에서 연결이 끊어지면 소켓 제거
void MainWindow::slot_discardSocket()
{
    m_socket->deleteLater();
    m_socket=nullptr;

    ui->statusBar->showMessage("Disconnected!");
}


// 연결된 소켓에서 발생한 오류에 따라 문장 출력
void MainWindow::slot_displayError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        QMessageBox::information(this, "QTCPClient", "The host was not found. Please check the host name and port settings.");
        break;
    case QAbstractSocket::ConnectionRefusedError:
        QMessageBox::information(this, "QTCPClient", "The connection was refused by the peer. Make sure QTCPServer is running, and check that the host name and port settings are correct.");
        break;
    default:
        QMessageBox::information(this, "QTCPClient", QString("The following error occurred: %1.").arg(m_socket->errorString()));
        break;
    }
}


// 첨부파일 또는 메시지 수신 함수
void MainWindow::slot_readSocket()
{
    // QByteArray 자료형 선언
    QByteArray buffer;

    // socket을 stream으로 연결
    QDataStream socketStream(m_socket);
    socketStream.setVersion(QDataStream::Qt_5_15);

    // transaction을 시작
    socketStream.startTransaction();
    socketStream >> buffer;

    // stream startTransaction 실행 문제시 에러 표시 후 함수 종료
    if(!socketStream.commitTransaction())
    {
        QString message = QString("%1 :: Waiting for more data to come..").arg(m_socket->socketDescriptor());
        emit signal_newMessage(message);
        return;
    }

    // 수신한 메시지의 0~128byte를 header에 저장
    QString header = buffer.mid(0,128);
    // ,로 split한 후에 나온 0번째 인덱스 값을 :로 split하여 1번째 값을 fileType에 저장
    QString fileType = header.split(",")[0].split(":")[1];

    // buffer의 128 byte 이후 부분을 저장
    buffer = buffer.mid(128);


    // fileType이 attachment인 경우
    if(fileType=="attachment")
    {
        // 파일 이름
        QString fileName = header.split(",")[1].split(":")[1];
        // 파일 형식
        QString ext = fileName.split(".")[1];
        // 파일 크기
        QString size = header.split(",")[2].split(":")[1].split(";")[0];

        // 파일 전송 관련 메시지 박스에서 Yes를 선택하면
        if (QMessageBox::Yes == QMessageBox::question(this, "QTCPServer", QString("You are receiving an attachment from sd:%1 of size: %2 bytes, called %3. Do you want to accept it?").arg(m_socket->socketDescriptor()).arg(size).arg(fileName)))
        {
            // 저장될 파일 경로 및 파일 이름 + 확장자 지정
            QString filePath = QFileDialog::getSaveFileName(this, tr("Save File"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/"+fileName, QString("File (*.%1)").arg(ext));

            // 해당 경로의 파일을 file 객체로 설정
            QFile file(filePath);

            // file 객체를 열음
            if(file.open(QIODevice::WriteOnly))
            {
                // buffer에 있는 데이터를 수신
                file.write(buffer);

                // 파일이 저장되는 것에 대한 메시지를 ui에 출력
                QString message = QString("INFO :: Attachment from sd:%1 successfully stored on disk under the path %2").arg(m_socket->socketDescriptor()).arg(QString(filePath));
                emit signal_newMessage(message);
            }
            else
                QMessageBox::critical(this,"QTCPServer", "An error occurred while trying to write the attachment.");
        }
        else
        {
            // 파일 전송 관련 메시지 박스에서 No를 선택
            QString message = QString("INFO :: Attachment from sd:%1 discarded").arg(m_socket->socketDescriptor());
            emit signal_newMessage(message);
        }
    }
    else if(fileType=="message")
    {
        // 전송된 메시지를 출력
        QString message = QString("%1 :: %2").arg(m_socket->socketDescriptor()).arg(QString::fromStdString(buffer.toStdString()));
        emit signal_newMessage(message);
    }
}


// 메시지 송신
void MainWindow::on_pushButton_sendMessage_clicked()
{
    if(m_socket)
    {
        // 소켓이 열려있다면
        if(m_socket->isOpen())
        {
            // ui에서 입력한 텍스트를 저장
            QString str = ui->lineEdit_message->text();

            // stream에 socket을 연결
            QDataStream socketStream(m_socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            // 헤더 부분에 fileType을 message로 설정
            QByteArray header;
            header.prepend(QString("fileType:message,fileName:null,fileSize:%1;").arg(str.size()).toUtf8());
            // 128byte로 조정
            header.resize(128);

            // message 인코딩 설정하고, QByteArray에 할당
            QByteArray byteArray = str.toUtf8();
            // header 정보를 앞에 붙임
            byteArray.prepend(header);

            // stream으로 byteArray 정보 전송
            socketStream << byteArray;

            // 메시지 입력창 리셋
            ui->lineEdit_message->clear();
        }
        else
            QMessageBox::critical(this,"QTCPClient","Socket doesn't seem to be opened");
    }
    else
        QMessageBox::critical(this,"QTCPClient","Not connected");
}

void MainWindow::on_pushButton_sendAttachment_clicked()
{
    if(m_socket)
    {
        if(m_socket->isOpen())
        {
            // 파일 경로 가져오고, 경로 문제시 경고 출력
            QString filePath = QFileDialog::getOpenFileName(this, ("Select an attachment"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), ("File (*.json *.txt *.png *.jpg *.jpeg)"));
            if(filePath.isEmpty())
            {
                QMessageBox::critical(this,"QTCPClient","You haven't selected any attachment!");
                return;
            }

            // 전송 할 file 객체를 경로 지정해서 열고(송신, 읽기 전용, read)
            QFile m_file(filePath);
            if(m_file.open(QIODevice::ReadOnly))
            {
                // 선택한 파일 이름
                QFileInfo fileInfo(m_file.fileName());
                QString fileName(fileInfo.fileName());

                // stream으로 보내는데
                QDataStream socketStream(m_socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                // 헤더 부분에 fileType을 attachment로 설정
                QByteArray header;
                header.prepend(QString("fileType:attachment,fileName:%1,fileSize:%2;").arg(fileName).arg(m_file.size()).toUtf8());
                header.resize(128);

                // QByteArray에 file을 byte로 할당
                QByteArray byteArray = m_file.readAll();
                // header 정보를 앞에 붙임
                byteArray.prepend(header);

                // stream으로 byteArray 정보 전송
                socketStream << byteArray;
            }
            else
                QMessageBox::critical(this,"QTCPClient","Attachment is not readable!");
        }
        else
            QMessageBox::critical(this,"QTCPClient","Socket doesn't seem to be opened");
    }
    else
        QMessageBox::critical(this,"QTCPClient","Not connected");
}

// [ex.02.12]
void MainWindow::slot_displayMessage(const QString& str)
{
    ui->textBrowser_receivedMessages->append(str);
}
