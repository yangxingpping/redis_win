/* credis-test.c -- a sample test application using credis (C client library 
 * for Redis)
 *
 * Copyright (c) 2009-2010, Jonas Romfelt <jonas at romfelt dot se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Credis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma comment(lib, "Ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <sys/time.h>
#else
#include <windows.h>
#endif

#include "credis.h"
#include "roomInfo.pb.h"

#include <memory>
#include <string>


long timer(int reset) 
{
  static long start=0; 
  struct timeval tv;

  //gettimeofday(&tv, NULL);

  /* return timediff */
  //if (!reset) {
  //  long stop = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;
  //  return (stop - start);
  //}

  ///* reset timer */
  //start = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;

  return 0;
}

unsigned long getrandom(unsigned long max)
{
  return (1 + (unsigned long) ( ((double)max) * (rand() / (RAND_MAX + 1.0))));
}

void randomize()
{
  struct timeval tv;
  //gettimeofday(&tv, NULL);
  //srand(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

#define DUMMY_DATA "some dummy data string"
#define LONG_DATA 50000

REDIS redis;

enum opType
{
	CGServerStart,
	CCreateRoom,
	CJoinRoom
};

int g_user_id = 10086;
int g_game_id = 11105101;
int g_room_id = 1;
int g_round_count = 10;
int g_card_count = 2;
int g_game_rule = 0;
int g_card_num = 6666;
int g_cur_room_start_inst = 1;
int g_cur_room_desk_count = 10;
std::string g_cur_g_server_ip = "192.168.1.125";
int	g_cur_g_server_port = 10086;

//获取空闲房间号
int get_free_room_num()
{
	char* p_str_free_room_num = nullptr;
	int result = credis_lpop(redis, "FreeRoomNumList", &p_str_free_room_num);
	if (result == 0 && p_str_free_room_num)
	{
		return atoi(p_str_free_room_num);
	}
	return 0;
}

//gServer启动
void gserver_start(int roomID,int gameID)
{
	auto result = credis_exists(redis, "startInst");
	if (result != 0) //说明不存在,直接设置为0
	{
		credis_watch(redis, "startInst");
		credis_multi(redis);
		credis_set(redis, "startInst", "1");
		credis_exec(redis);
	}
	result = credis_incr(redis, "startInst", &g_cur_room_start_inst);
	using namespace CardModel;
	desk_info desk;
	desk.set_game_id(gameID);
	desk.set_start_inst(g_cur_room_start_inst);
	std::string key = std::to_string(long long(roomID))+ ":"+std::to_string(long long(gameID)); //表2的key
	std::string strSerial = desk.SerializeAsString();
	result = credis_set(redis, key.c_str(), strSerial.c_str());
	result = credis_expire(redis, key.c_str(), 30);

	//从表4中获取数据
	std::string str_room_id = std::to_string(long long(roomID));
	char* p_desk_started=nullptr;
	result = credis_spop(redis, str_room_id.c_str(), &p_desk_started);
	while (result == 0 && p_desk_started!=nullptr)
	{
		desk_info d;
		auto bSucc = d.ParseFromString(std::string(p_desk_started));
		if (bSucc)
		{
			std::string str_room_num = std::to_string(long long(d.card_number()));
			credis_del(redis, str_room_num.c_str());
		}
		p_desk_started = nullptr;
		result = credis_spop(redis, str_room_id.c_str(), &p_desk_started);
	}
	//填充表3中的数据
	std::string free_room_key = std::to_string(long long(gameID));
	for (int i = 0; i < g_cur_room_desk_count; ++i)
	{
		desk_info desk;
		desk.set_desk_index(i);
		desk.set_room_id(roomID);
		desk.set_server_ip(g_cur_g_server_ip.c_str());
		desk.set_server_port(g_cur_g_server_port);
		desk.set_start_inst(g_cur_room_start_inst);
		desk.set_maxplayer_count(4);
		desk.set_start_inst(g_cur_room_start_inst);
		auto str_serial = desk.SerializeAsString();
		result = credis_lpush(redis, free_room_key.c_str(), strSerial.c_str());
	}
}

//创建房间
void create_room(int userID, int gameID, int roundCount, int cardCount, int gameRule)
{
	using namespace CardModel;
	auto str_game_id = std::to_string(long long(gameID));
	char* p_desk_info = nullptr;
	int result = 0;
	result = credis_lpop(redis, str_game_id.c_str(), &p_desk_info); //从表3中拿出空闲桌子的信息
	if (result == 0 && p_desk_info != nullptr) //成功获取了桌子的信息
	{
		desk_info desk;
		auto b_succ = desk.ParseFromString(std::string(p_desk_info));
		if (b_succ)
		{
			auto room_id = desk.room_id();
			auto game_id = desk.game_id();
			auto str_room_game_key = std::to_string(long long(room_id)) + ":" +
				std::to_string(long long(game_id));
			//从表2中获取信息
			char* p_desk_start_info;
			result = credis_get(redis, str_room_game_key.c_str(), &p_desk_start_info);
			if (result == 0 && p_desk_start_info)
			{
				desk_info desk_start_info;
				b_succ = desk_start_info.ParseFromString(std::string(p_desk_start_info));
				if (b_succ)
				{
					if (desk.start_inst() == desk_start_info.start_inst()) //说明当前这个房间可以使用
					{
						auto desk_card_num = get_free_room_num();
						if (desk_card_num == 0) //出错了,获取空闲房间号失败,把桌子返回给freedeskList(表3)
						{
							credis_lpush(redis, str_game_id.c_str(), p_desk_info);
						}
						else //填充桌子上游戏的相关信息(gameid, roundCount, cardCount, gameRule等)
						{
							desk.set_round_count(roundCount);
							desk.set_card_cost(cardCount);
							desk.set_card_number(desk_card_num);
							desk.set_desk_rule(gameRule);
							auto str_deskinfo_serial = desk.SerializeAsString();
							auto str_desk_num = std::to_string(long long(desk_card_num));
							result = credis_set(redis, str_desk_num.c_str(), str_deskinfo_serial.c_str());
						}
					}
				}
			}
			else //出现错误
			{

			}
		}
	}
	else if (result != 0) //redis网络错误
	{

	}
	else if (p_desk_info == nullptr) //没有空闲桌子
	{

	}
	else
	{

	}
}

//加入房间
void join_room(int userID, int gameID, int cardNum)
{
	using namespace CardModel;
	auto	str_desk_num = std::to_string(long long(cardNum));
	char*	p_req_desk_info = nullptr;
	int		result = credis_get(redis, str_desk_num.c_str(), &p_req_desk_info);
	if (result == 0 && p_req_desk_info)
	{
		desk_info desk;
		bool b_succ = desk.ParseFromString(std::string(p_req_desk_info));
		if (b_succ)
		{
			int room_id = desk.room_id();
			int game_id = desk.game_id();
			auto str_room_game_key = std::to_string(long long(room_id)) + ":" +
				std::to_string(long long(game_id));
			char* p_room_start_info = nullptr;
			result = credis_get(redis, str_room_game_key.c_str(), &p_room_start_info);
			if (result == 0 && p_room_start_info)
			{
				desk_info room_start;
				b_succ = room_start.ParseFromString(std::string(p_room_start_info));
				if (b_succ)
				{
					if (room_start.start_inst() == desk.start_inst()) //成功，返回信息给客户端
					{

					}
					else //说明这个桌子的信息是错误的
					{

					}
				}
				else //pb解析失败
				{

				}
			}
			else //出错
			{

			}
		}
		else //pb解析出错
		{

		}
	}
	else //从redis获取数据出错
	{

	}
}

void call_func(opType type)
{
	switch (type)
	{
	case CGServerStart:
		gserver_start(g_room_id, g_game_id);
		break;
	case CCreateRoom:
		create_room(g_user_id, g_game_id, g_round_count, g_card_count, g_game_rule);
		break;
	case CJoinRoom:
		join_room(g_user_id, g_game_id, g_card_num);
		break;
	default:
		break;
	}
}

int main(int argc, char **argv) {
  
  REDIS_INFO info;
  char *val, **valv, lstr[50000];
  const char *keyv[] = {"kalle", "adam", "unknown", "bertil", "none"};
  int rc, keyc=5, i;
  double score1, score2;

  redis = credis_connect("192.168.1.125", 6379, 1000);
  if (redis == NULL) {
    printf("Error connecting to Redis server. Please start server to run tests.\n");
    exit(1);
  }
  /*char* pT = nullptr;
  rc = credis_get(redis, "notexist", &pT);*/
  call_func(CGServerStart);

  printf("\n\n************* hash ************************************ \n");
  rc = credis_hset(redis, "ActiveRooms", "3", "3:3");
  rc = credis_hget(redis, "ActiveRooms", "3", &val);
  rc = credis_hexists(redis, "ActiveRooms", "3");
  rc = credis_hdel(redis, "ActiveRooms", "4");

  int valuexy;
  rc = credis_incr(redis, "startInst", &valuexy);
  rc = credis_incr(redis, "startInst", &valuexy);
  printf("\n\n************* get/set ************************************ \n");

  rc = credis_set(redis, "kalle", "kulax");
  printf("set kalle=kula returned: %d\n", rc);


 /* rc = credis_watch(redis, "kalle");
  rc = credis_multi(redis);
  rc = credis_set(redis, "kalle", "yxp");
  rc = credis_set(redis, "age", "55");
  rc = credis_exec(redis);*/
 // rc = credis_discard(redis);
  rc = credis_get(redis, "kalle", &val);
  printf("get kalle returned: %s\n", val);

  rc = credis_type(redis, "someunknownkey");
  printf("get type unknown key returned: %d\n", rc);

  rc = credis_type(redis, "kalle");
  printf("get type known key returned: %d\n", rc);

  rc = credis_getset(redis, "kalle", "buhu", &val);
  printf("getset kalle=buhu returned: %s\n", val);

  rc = credis_get(redis, "kalle", &val);
  printf("get kalle returned: %s\n", val);

  rc = credis_del(redis, "kalle");
  printf("del kalle returned: %d\n", rc);

  rc = credis_get(redis, "kalle", &val);
  printf("get kalle returned: %s\n", val);

  rc = credis_set(redis, "adam", "aaa");
  rc = credis_set(redis, "bertil", "bbbbbbb");
  rc = credis_set(redis, "caesar", "cccc");
  rc = credis_get(redis, "adam", &val);
  printf("get adam returned: %s\n", val);
  rc = credis_get(redis, "bertil", &val);
  printf("get bertil returned: %s\n", val);
  rc = credis_get(redis, "caesar", &val);
  printf("get caesar returned: %s\n", val);

  rc = credis_mget(redis, keyc, keyv, &valv);
  printf("mget returned: %d\n", rc);
  for (i = 0; i < rc; i++)
    printf(" % 2d: %s\n", i, valv[i]);

  rc = credis_keys(redis, "*", &valv);
  printf("keys returned: %d\n", rc);
  for (i = 0; i < rc; i++)
    printf(" % 2d: %s\n", i, valv[i]);

  printf("\n\n************* sets ************************************ \n");


  rc = credis_sadd(redis, "fruits", "banana");
  printf("sadd returned: %d\n", rc);

  rc = credis_sismember(redis, "fruits", "banana");
  printf("sismember returned: %d\n", rc);

  rc = credis_sadd(redis, "fruits", "apple");
  printf("sadd returned: %d\n", rc);

  rc = credis_srem(redis, "fruits", "banana");
  printf("srem returned: %d\n", rc);

  rc = credis_sismember(redis, "fruits", "banana");
  printf("sismember returned: %d\n", rc);

  rc = credis_srem(redis, "fruits", "orange");
  printf("srem returned: %d\n", rc);


  printf("\n\n************* lists ************************************ \n");

  rc = credis_llen(redis, "mylist");
  printf("length of list: %d\n", rc);

  rc = credis_del(redis, "mylist");
  printf("del returned: %d\n", rc);

  rc = credis_llen(redis, "mylist");
  printf("length of list: %d\n", rc);

  rc = credis_rpush(redis, "kalle", "first");
  printf("rpush returned: %d\n", rc);

  rc = credis_rpush(redis, "mylist", "first");
  printf("rpush returned: %d\n", rc);

  rc = credis_rpush(redis, "mylist", "right");
  printf("rpush returned: %d\n", rc);

  rc = credis_lpush(redis, "mylist", "left");
  printf("lpush returned: %d\n", rc);

  rc = credis_lrange(redis, "mylist", 0, 2, &valv);
  printf("lrange (0, 2) returned: %d\n", rc);
  for (i = 0; i < rc; i++)
    printf(" % 2d: %s\n", i, valv[i]);

  rc = credis_lrange(redis, "mylist", 0, -1, &valv);
  printf("lrange (0, -1) returned: %d\n", rc);
  for (i = 0; i < rc; i++)
    printf(" % 2d: %s\n", i, valv[i]);

  /* generate some test data */
  randomize();
  for (i = 0; i < LONG_DATA; i++)
    lstr[i] = ' ' + getrandom('~' - ' ');
  lstr[i-1] = 0;
  rc = credis_lpush(redis, "mylist", lstr);
  printf("rpush returned: %d\n", rc);

  rc = credis_lrange(redis, "mylist", 0, 0, &valv);
  printf("lrange (0, 0) returned: %d, strncmp() returend %d\n", rc, strncmp(valv[0], lstr, LONG_DATA-1));

  rc = credis_llen(redis, "mylist");
  printf("length of list: %d\n", rc);

  rc = credis_lrange(redis, "not_exists", 0, -1, &valv);
  printf("lrange (0, -1) returned: %d\n", rc);
  for (i = 0; i < rc; i++)
    printf(" % 2d: %s\n", i, valv[i]);

  rc = credis_del(redis, "mylist");
  printf("del returned: %d\n", rc);

  rc = credis_llen(redis, "mylist");
  printf("length of list: %d\n", rc);

  printf("Adding 200 items to list\n");
  for (i = 0; i < 200; i++) {
    char str[100];
    sprintf(str, "%d%s%d", i, DUMMY_DATA, i);
    rc = credis_rpush(redis, "mylist", str);
    if (rc != 0)
      printf("rpush returned: %d\n", rc);
  }

  rc = credis_lrange(redis, "mylist", 0, 200, &valv);
  printf("lrange (0, 200) returned: %d, verifying data ... ", rc);
  for (i = 0; i < rc; i++) {
    char str[100];
    sprintf(str, "%d%s%d", i, DUMMY_DATA, i);
    if (strncmp(valv[i], str, strlen(str)))
      printf("\nreturned item (%d) data differs: '%s' != '%s'", i, valv[i], str);
  }  
  printf("all data verified!\n");

  printf("Testing lpush and lrem\n");
  rc = credis_lpush(redis, "cars", "volvo");
  rc = credis_lpush(redis, "cars", "saab");
  rc = credis_lrange(redis, "cars", 0, 200, &valv);
  printf("lrange (0, 200) returned: %d items\n", rc);
  for (i = 0; i < rc; i++) 
      printf("  %02d: %s\n", i, valv[i]);
  rc = credis_lrem(redis, "cars", 1, "volvo");
  printf("credis_lrem() returned %d\n", rc);
  rc = credis_lrange(redis, "cars", 0, 200, &valv);
  printf("lrange (0, 200) returned: %d items\n", rc);
  for (i = 0; i < rc; i++) 
      printf("  %02d: %s\n", i, valv[i]);
  rc = credis_lrem(redis, "cars", 1, "volvo");

  printf("Testing lset\n");
  rc = credis_lset(redis, "cars", 2, "koenigsegg");
  printf("lrange (0, 200) returned: %d items\n", rc);
  for (i = 0; i < rc; i++) 
      printf("  %02d: %s\n", i, valv[i]);
  rc = credis_lrem(redis, "cars", 1, "volvo");



  printf("\n\n************* sorted sets ********************************** \n");

  score1 = 3.5;
  rc = credis_zincrby(redis, "zkey", score1, "member1", &score2);
  printf("zincrby returned: %d, score=%f, new_score=%f\n", rc, score1, score2);
  rc = credis_zincrby(redis, "zkey", score1, "member1", &score2);
  printf("zincrby returned: %d, score=%f, new_score=%f\n", rc, score1, score2);
  score2 = 123;
  rc = credis_zscore(redis, "zkey", "member1", &score2);
  printf("zscore returned: %d, score=%f\n", rc, score2);
  rc = credis_zscore(redis, "zkey_unknown", "member1", &score2);
  printf("zscore (unknown key) returned: %d, score=%f\n", rc, score2);
  rc = credis_zscore(redis, "zkey", "member_unknown", &score2);
  printf("zscore (unknown member) returned: %d, score=%f\n", rc, score2);

  rc = credis_zrank(redis, "zkey", "member1");
  printf("zrank returned: %d\n", rc);
  rc = credis_zrevrank(redis, "zkey", "member1");
  printf("zrevrank returned: %d\n", rc);
  if (rc < 0)
    printf("Error message: %s\n", credis_errorreply(redis));
 
  credis_close(redis);

  return 0;
}
