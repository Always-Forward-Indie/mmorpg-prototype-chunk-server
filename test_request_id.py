#!/usr/bin/env python3
"""
Тестовый клиент для проверки системы lag compensation с requestId
"""

import socket
import json
import time
import uuid
import hashlib

def generate_request_id(session_id=123):
    """Генерирует requestId в формате sync_timestamp_session_sequence_hash"""
    timestamp = int(time.time() * 1000)  # миллисекунды
    sequence = int(time.time() % 10000)  # простой sequence
    hash_part = hashlib.md5(f"{timestamp}_{session_id}_{sequence}".encode()).hexdigest()[:8].upper()
    return f"sync_{timestamp}_{session_id}_{sequence}_{hash_part}"

def test_move_character():
    """Тестирует отправку пакета moveCharacter с requestId"""
    
    # Подключение к серверу
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect(('localhost', 27017))
        print("✅ Подключение к chunk server успешно")
        
        # Создание тестового пакета с requestId
        request_id = generate_request_id()
        client_send_ms = int(time.time() * 1000)
        
        packet = {
            "header": {
                "requestId": request_id,
                "clientSendMs": client_send_ms,
                "eventType": "moveCharacter"
            },
            "body": {
                "clientId": 1,
                "characterId": 1,
                "position": {
                    "positionX": 100.5,
                    "positionY": 200.3,
                    "positionZ": 0.0,
                    "rotationZ": 45.0
                }
            }
        }
        
        # Отправка пакета
        message = json.dumps(packet) + "\n"
        sock.send(message.encode())
        print(f"📤 Отправлен пакет с requestId: {request_id}")
        print(f"🕐 Client send time: {client_send_ms}")
        
        # Получение ответа
        response = sock.recv(4096).decode().strip()
        response_data = json.loads(response)
        
        # Время получения ответа
        client_recv_ms = int(time.time() * 1000)
        
        print("\n📥 Получен ответ:")
        print(json.dumps(response_data, indent=2, ensure_ascii=False))
        
        # Анализ lag compensation
        header = response_data.get("header", {})
        body = response_data.get("body", {})
        
        request_id_echo = header.get("requestIdEcho") or body.get("requestIdEcho")
        server_recv_ms = header.get("serverRecvMs") or body.get("serverRecvMs")
        server_send_ms = header.get("serverSendMs") or body.get("serverSendMs")
        client_send_ms_echo = header.get("clientSendMsEcho") or body.get("clientSendMsEcho")
        
        print("\n🔍 Анализ lag compensation:")
        print(f"Original requestId:     {request_id}")
        print(f"Echoed requestId:       {request_id_echo}")
        print(f"RequestId match:        {request_id == request_id_echo}")
        print(f"Client send time:       {client_send_ms}")
        print(f"Server recv time:       {server_recv_ms}")
        print(f"Server send time:       {server_send_ms}")
        print(f"Client recv time:       {client_recv_ms}")
        print(f"Client send echo:       {client_send_ms_echo}")
        
        if all([server_recv_ms, server_send_ms, client_send_ms_echo]):
            # Расчет latency
            client_to_server_latency = server_recv_ms - client_send_ms
            server_processing_time = server_send_ms - server_recv_ms
            server_to_client_latency = client_recv_ms - server_send_ms
            total_round_trip = client_recv_ms - client_send_ms
            
            print(f"\n⏱️ Временные характеристики:")
            print(f"Client → Server latency: {client_to_server_latency} ms")
            print(f"Server processing time:  {server_processing_time} ms")
            print(f"Server → Client latency: {server_to_client_latency} ms")
            print(f"Total round trip time:   {total_round_trip} ms")
            
            # Проверка синхронизации
            if request_id == request_id_echo:
                print("✅ RequestId синхронизация: УСПЕШНО")
            else:
                print("❌ RequestId синхронизация: ОШИБКА")
                
            if client_send_ms == client_send_ms_echo:
                print("✅ Timestamp синхронизация: УСПЕШНО")
            else:
                print("❌ Timestamp синхронизация: ОШИБКА")
        else:
            print("❌ Неполные данные для анализа lag compensation")
            
    except Exception as e:
        print(f"❌ Ошибка: {e}")
    finally:
        sock.close()

def test_join_character():
    """Тестирует отправку пакета joinCharacter с requestId"""
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect(('localhost', 27017))
        print("✅ Подключение к chunk server успешно")
        
        request_id = generate_request_id()
        client_send_ms = int(time.time() * 1000)
        
        packet = {
            "header": {
                "requestId": request_id,
                "clientSendMs": client_send_ms,
                "eventType": "joinCharacter"
            },
            "body": {
                "clientId": 1,
                "characterId": 1,
                "character": {
                    "characterId": 1,
                    "characterName": "TestPlayer",
                    "characterClass": "Warrior",
                    "characterLevel": 5,
                    "currentHealth": 100,
                    "maxHealth": 100
                },
                "position": {
                    "positionX": 0.0,
                    "positionY": 0.0,
                    "positionZ": 0.0,
                    "rotationZ": 0.0
                }
            }
        }
        
        message = json.dumps(packet) + "\n"
        sock.send(message.encode())
        print(f"📤 Отправлен joinCharacter с requestId: {request_id}")
        
        response = sock.recv(4096).decode().strip()
        response_data = json.loads(response)
        
        print("\n📥 Получен ответ:")
        print(json.dumps(response_data, indent=2, ensure_ascii=False))
        
        # Проверка requestId в ответе
        header = response_data.get("header", {})
        body = response_data.get("body", {})
        request_id_echo = header.get("requestIdEcho") or body.get("requestIdEcho")
        
        if request_id == request_id_echo:
            print("✅ RequestId эхо работает корректно для joinCharacter")
        else:
            print(f"❌ RequestId эхо не совпадает: отправлен {request_id}, получен {request_id_echo}")
            
    except Exception as e:
        print(f"❌ Ошибка: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    print("🧪 Тестирование системы lag compensation с requestId")
    print("=" * 60)
    
    print("\n1. Тестирование moveCharacter...")
    test_move_character()
    
    print("\n" + "=" * 60)
    print("\n2. Тестирование joinCharacter...")
    test_join_character()
    
    print("\n✅ Тестирование завершено!")
