#include "mainwindow.h"
#include "ui_mainwindow.h"

// [ex.02.1]
// MainWindow 생성자 실행
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // TcpServer Socket 초기화
    m_server = new QTcpServer();

    // TcpServer Socket 생성 및 접속 요청 대기
    if(m_server->listen(QHostAddress::Any, 8080))
    {
        // Server Socket으로 연결 요청이 들어오면 slot_newConnetction 함수 실행
        connect(m_server, &QTcpServer::newConnection, this, &MainWindow::slot_newConnection);

        // 메시지가 수신되면 slot_displayMessage 함수 실행
        connect(this, &MainWindow::singal_newMessage, this, &MainWindow::slot_displayMessage);

        // 서버 실행 메시지 출력
        ui->statusBar->showMessage("Server is listening...");
    }


    // 서버 소켓 생성 실패
    else
    {
        QMessageBox::critical(this,"QTCPServer",QString("Unable to start the server: %1.").arg(m_server->errorString()));
        exit(EXIT_FAILURE);
    }
}


MainWindow::~MainWindow()
{
    // 서버에 연결된 모든 연결 소켓 해제
    foreach (QTcpSocket* socket, qset_connectedSKT)
    {
        socket->close();
        socket->deleteLater();
    }

    // 서버 소켓 해제
    m_server->close();
    m_server->deleteLater();

    delete ui;
}


void MainWindow::slot_newConnection()
{
    // 서버 소켓 연결 대기
    while (m_server->hasPendingConnections())
        // 연결되는 소켓을 리스트에 append
        appendToSocketList(m_server->nextPendingConnection());
}

// 소켓 연결시 리스트에 소켓 insert
void MainWindow::appendToSocketList(QTcpSocket* socket)
{

    // 연결된 소켓을 qSet에 insert
    // qSet과 qMap의 차이점
    // qSet : 중복된 값 insert 불가(고유값)
    // qMap : key와 value로 이루어진 Dictionary
    qset_connectedSKT.insert(socket);


    // 소켓에 읽을 메시지가 수신 시에 slot_readSocket 실행
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::slot_readSocket);

    // 소켓 연결이 끊기면 slot_discardSocket 실행
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::slot_discardSocket);

    // 연결된 소켓에 오류가 발생하면 slot_displayError 실행
    connect(socket, &QAbstractSocket::errorOccurred, this, &MainWindow::slot_displayError);

    // 소켓 디스크립터로 대상 선택 가능하도록 ui 표시
    ui->comboBox_receiver->addItem(QString::number(socket->socketDescriptor()));

    // 연결된 클라이언트 정보와, 소켓 디스크립터(정수 식별자) 출력
    slot_displayMessage(QString("INFO :: Client with sockd:%1 has just entered the room").arg(socket->socketDescriptor()));
}


// 연결된 소켓에서 연결이 끊어지면 동작
void MainWindow::slot_discardSocket()
{
    // disconnected된 socket을 찾음
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());

    // 해당 소켓을 qset_connectedSKT에서 삭제
    QSet<QTcpSocket*>::iterator it = qset_connectedSKT.find(socket);
    if (it != qset_connectedSKT.end()){
        slot_displayMessage(QString("INFO :: A client has just left the room").arg(socket->socketDescriptor()));
        qset_connectedSKT.remove(*it);
    }

    // ui 콤보박스 재설정
    refreshComboBox();

    socket->deleteLater();
}


// 연결된 소켓에서 오류 종류에 따른 오류 관련 상태 출력
void MainWindow::slot_displayError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        QMessageBox::information(this, "QTCPServer", "The host was not found. Please check the host name and port settings.");
        break;
    case QAbstractSocket::ConnectionRefusedError:
        QMessageBox::information(this, "QTCPServer", "The connection was refused by the peer. Make sure QTCPServer is running, and check that the host name and port settings are correct.");
        break;
    default:
        QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
        QMessageBox::information(this, "QTCPServer", QString("The following error occurred: %1.").arg(socket->errorString()));
        break;
    }
}


// 첨부파일 또는 메시지 수신 처리
void MainWindow::slot_readSocket()
{
    // readReady 상태에서 signal이 발생한 socket을 찾음
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());

    // QByteArray 타입의 buffer
    QByteArray buffer;

    // 서버에 연결된 socket을 stream으로 연결
    // stream을 사용하면 데이터 형식에 따라 자동으로 직렬화하여 데이터 형식 변환 및 형식 지원에 따른 복잡한 코드를 줄일 수 있음
    QDataStream socketStream(socket);
    // 스트림 버전을 Qt 5.15로 맞춤
    socketStream.setVersion(QDataStream::Qt_5_15);

    // stream 트랜잭션 시작
    // 데이터 스트림이 일시 중단되면서 데이터 전송 작업은 트랜잭션 내에서 수행
    socketStream.startTransaction();
    socketStream >> buffer;

    // stream startTransaction 실행 문제시 에러 표시 후 함수 종료
    // 데이터를 다 보내지 못하면 return하여 함수 재실행, 다 보내면 아래 로직 진행
    if(!socketStream.commitTransaction())
    {
        QString message = QString("%1 :: Waiting for more data to come..").arg(socket->socketDescriptor());
        emit singal_newMessage(message);
        return;
    }

    // 수신된 데이터 0부터 128byte까지만 header에 담기
    QString header = buffer.mid(0,128);
    // header에 담은 데이터를 ,로 split하고 나온 결과 중 0번째 인덱스의 값을
    // :로 split하여 나온 결과 중 1번째 인덱스 값을 fileType에 담기
    QString fileType = header.split(",")[0].split(":")[1];

    // buffer의 128 byte 이후 부분을
    buffer = buffer.mid(128);

    // fileType이 attachment 라면
    if(fileType=="attachment")
    {
        // 파일 이름 정보 저장
        QString fileName = header.split(",")[1].split(":")[1];
        // 파일 확장자 저장
        QString ext = fileName.split(".")[1];
        // 파일 크기 저장
        QString size = header.split(",")[2].split(":")[1].split(";")[0];

        // 파일 전송 메시지를 받으면, 메시지 박스에서 수신 여부 확인
        // 메시지 박스에서 yes를 선택하면
        if (QMessageBox::Yes == (QMessageBox::question(this, "QTCPServer", QString("You are receiving an attachment from sd:%1 of size: %2 bytes, called %3. Do you want to accept it?").arg(socket->socketDescriptor()).arg(size).arg(fileName))))
        {
            // 저장될 파일의 경로, 파일 이름, 확장자 설정
            QString filePath = QFileDialog::getSaveFileName(this, tr("Save File"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/"+fileName, QString("File (*.%1)").arg(ext));

            // 해당 경로의 file을 객체로 설정
            QFile file(filePath);

            // 쓰기 전용(송신)으로 file 객체를 열음
            if(file.open(QIODevice::WriteOnly))
            {
                // 담긴 데이터를 buffer로 송신
                file.write(buffer);

                // 파일이 저장되는 것에 대한 메시지를 ui에 출력한다.
                QString message = QString("INFO :: Attachment from sd:%1 successfully stored on disk under the path %2").arg(socket->socketDescriptor()).arg(QString(filePath));
                emit singal_newMessage(message);
            }
            else
                QMessageBox::critical(this,"QTCPServer", "An error occurred while trying to write the attachment.");
        }
        else
        {
            // 메시지 박스에서 No를 선택하면, 전송 거부
            QString message = QString("INFO :: Attachment from sd:%1 discarded").arg(socket->socketDescriptor());
            emit singal_newMessage(message);
        }
    }
    else if(fileType=="message")
    {
        // 전송된 메시지를 서버에서 출력
        QString message = QString("%1 :: %2").arg(socket->socketDescriptor()).arg(QString::fromStdString(buffer.toStdString()));
        emit singal_newMessage(message);
    }
}


// 서버에서 메시지를 송신
void MainWindow::on_pushButton_sendMessage_clicked()
{
    // 수신할 대상을 comboBox에서 선택
    QString receiver = ui->comboBox_receiver->currentText();

    // Broadcast 선택 시,
    if(receiver=="Broadcast")
    {
        // qset_connectedSKT에 저장된 모든 클라이언트의 소켓에 전송
        foreach (QTcpSocket* socket,qset_connectedSKT)
        {
            sendMessage(socket);
        }
    }
    // 선택한 대상이 있을 때
    else
    {
        // qset_connectedSKT에서 해당 클라이언트의 소켓을 찾아 메시지 전송
        foreach (QTcpSocket* socket, qset_connectedSKT)
        {
            if(socket->socketDescriptor() == receiver.toLongLong())
            {
                sendMessage(socket);
                break;
            }
        }
    }

    // 메시지 입력창 리셋
    ui->lineEdit_message->clear();
}


// 서버에서 파일을 전송하는 함수
void MainWindow::on_pushButton_sendAttachment_clicked()
{
    // 보낼 대상 선택
    QString receiver = ui->comboBox_receiver->currentText();

    // 파일 경로 가져오고, 경로 문제시 경고 출력
    QString filePath = QFileDialog::getOpenFileName(this, ("Select an attachment"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), ("File (*.json *.txt *.png *.jpg *.jpeg)"));
    if(filePath.isEmpty())
    {
        QMessageBox::critical(this,"QTCPClient","You haven't selected any attachment!");
        return;
    }

    // 보낼 대상이 연결된 모든 socket일때 동작
    if(receiver=="Broadcast")
    {
        foreach (QTcpSocket* socket, qset_connectedSKT)
        {
            sendAttachment(socket, filePath);
        }
    }
    // 보낼 대상이 특정 socket일때 동작
    else
    {
        foreach (QTcpSocket* socket, qset_connectedSKT)
        {
            // qset_connectedSKT에서 소켓을 찾아 메시지 전송
            if(socket->socketDescriptor() == receiver.toLongLong())
            {
                sendAttachment(socket, filePath);
                break;
            }
        }
    }
    ui->lineEdit_message->clear();
}

// 메시지를 전송하는 함수
void MainWindow::sendMessage(QTcpSocket* socket)
{
    if(socket)
    {
        // 소켓이 열려있을 때
        if(socket->isOpen())
        {
            // ui에서 입력한 text를 str에 저장
            QString str = ui->lineEdit_message->text();

            // socket을 stream에 연결
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            //QByteArray 자료형의 변수 선언
            QByteArray header;
            // 변수에 fileType, fileName, fileSize를 header의 가장 앞에 붙임
            header.prepend(QString("fileType:message,fileName:null,fileSize:%1;").arg(str.size()).toUtf8());
            // 128byte로 사이즈 조정(클라이언트가 128바이트 전후로 나눠 읽기 때문에)
            header.resize(128);

            // QByteArray 자료형에 메시지 데이터를 담을 수 있도록 형변환
            QByteArray byteArray = str.toUtf8();
            // header를 메시지 데이터 가장 앞에 붙임
            byteArray.prepend(header);

            // stream으로 byteArray 정보 전송
            socketStream << byteArray;
        }
        else
            QMessageBox::critical(this,"QTCPServer","Socket doesn't seem to be opened");
    }
    else
        QMessageBox::critical(this,"QTCPServer","Not connected");
}

// [ex.02.11]
void MainWindow::sendAttachment(QTcpSocket* socket, QString filePath)
{
    if(socket)
    {
        if(socket->isOpen())
        {
            // 전송할 file 객체를 경로 지정해서 열고
            QFile m_file(filePath);
            if(m_file.open(QIODevice::ReadOnly))
            {
                // 선택한 파일 이름 얻기
                QFileInfo fileInfo(m_file.fileName());
                QString fileName(fileInfo.fileName());

                // stream에 socket 연결
                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                // 헤더 부분에 fileType을 attachment로 작성
                QByteArray header;
                header.prepend(QString("fileType:attachment,fileName:%1,fileSize:%2;").arg(fileName).arg(m_file.size()).toUtf8());
                header.resize(128);

                // QByteArray에 file을 byte로 할당하고
                QByteArray byteArray = m_file.readAll();
                // header 정보를 앞에 붙임
                byteArray.prepend(header);

                // stream으로 byteArray 정보 전송
                socketStream << byteArray;

            }
            else
                QMessageBox::critical(this,"QTCPClient","Couldn't open the attachment!");
        }
        else
            QMessageBox::critical(this,"QTCPServer","Socket doesn't seem to be opened");
    }
    else
        QMessageBox::critical(this,"QTCPServer","Not connected");
}


// textBox에 메시지를 출력하는 함수
void MainWindow::slot_displayMessage(const QString& str)
{
    ui->textBrowser_receivedMessages->append(str);
}

// ComboBox를 새로고침하는 함수
void MainWindow::refreshComboBox(){
    ui->comboBox_receiver->clear();
    ui->comboBox_receiver->addItem("Broadcast");
    foreach(QTcpSocket* socket, qset_connectedSKT)
        ui->comboBox_receiver->addItem(QString::number(socket->socketDescriptor()));
}
