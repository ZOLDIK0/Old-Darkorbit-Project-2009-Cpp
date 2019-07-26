#include "SessionsManager.h"
#include "Mob.h"
#include "Collectable.h"
#include "ResourceBox.h"
#include "DatabaseUtils.h"
#define _USE_MATH_DEFINES
#include <math.h>

void CSessionsManager::fastTick()
{
	std::vector<CSession>::iterator permSessionIt = m_permSessions.begin();
	std::vector<CSession>::iterator tempSessionIt = m_tempSessions.begin();

	while (true)
	{
		/* Iterate */
		std::vector<CSession>::iterator sessionIt;
		if (permSessionIt != m_permSessions.end())
		{
			sessionIt = permSessionIt;
			++permSessionIt;
		}
		else
		{
			/* permIterator is done iterate throguh temp */
			if (tempSessionIt != m_tempSessions.end())
			{
				sessionIt = tempSessionIt;
				++tempSessionIt;
			}
			else
			{
				/* tempIterator is done aswell */
				break;
			}
		}
		CSession& session = sessionIt.operator*();


		auto timeNow = getTimeNow();
		/* Start internal loop */
		map_t map = session.getMapId();

		//if (session.getAllConnections().size() == 0) continue; //skip if there aint even no players.
															   //this function should NOT be allowed to call mob->die() due to thread safety
		/* This function will not handle deletion/insertion of vector mobs in session */
		session.lockMobsRead();
		/* Reference instead of copy because the npccontainer contains a shared ptr and
			cannot be deleted since the mutex stops it from deleting until loop is over
		*/
		for (auto& mobs : session.getMobs()) 
		{
			std::shared_ptr<CMob> mp = mobs.second;
			if (mp != nullptr)
			{
				//if alien wants to make another move (when the time has come)
				Position_t p = mp->getPosition();
				//highest cpu take i guess
				id_t triggeredId = 0;
				if (mp->getShip().id != 80)
				{
					triggeredId = mp->getFocusedPlayer();
					if (!triggeredId) //dont handle cubikonikons
					{
						session.lockConnectionsRead();
						for (auto& playerPair : session.getAllConnections())
						{
							handlePtr& player = playerPair.second;
							if (player != nullptr)
							{
								pos_t a = std::abs(player->getX() - p.first);
								pos_t b = std::abs(player->getY() - p.second);
								pos_t distance = std::sqrt(a*a + b * b);
								if (!player->m_pbIsInvisible && distance < Constants::Game::FIGHT_RANGE_NPC)//TODO: && !player->isInvisible())
								{
									mp->setFocusToPlayer(player->getID());
									mp->setRealWaitingTime(timeNow, 0); // so it instantly flies towards enemy
									triggeredId = player->getID();
									break;
								}
							}
						}
						session.unlockConnectionsRead();

					}
				}
				if (timeHasPassed(timeNow, mp->getLastTimeShot(), 60000))
				{
					//TODO SetHP SetSHD
					mp->increaseHP(mp->getShip().hpmax * 0.05);
				}
				if (mp->getNextMovingTime() < timeNow)
				{

					handlePtr userIfTriggered;
					if (triggeredId)
						//Handle "players"
					{
						if (userIfTriggered = session.getHandler(triggeredId))
						{
							pos_t a = std::abs(userIfTriggered->getX() - p.first);
							pos_t b = std::abs(userIfTriggered->getY() - p.second);
							pos_t distance = std::sqrt(a*a + b * b);

							const pos_t DISTANCE_MAX_TILL_FOLLOW = 300;
							const pos_t DISTANCE_MAX_TILL_CIRCLE = 220;
							const pos_t DISTANCE_FOLLOW = 20;
							const pos_t DISTANCE_CLOSE = 200;
							if (distance > DISTANCE_MAX_TILL_FOLLOW)
							{
								//hey lets get a bit closer to our beloved friend enemy

								//for now its random, in the future make the degree with the shortest distance
								// (point enemy <-> point self    just make 2 points degree ez bye
								auto pos = mp->getPosition();
								auto mypos = Position_t(userIfTriggered->getX(), userIfTriggered->getY());
								double degree = random<uint32_t>(360) * M_PI / 180.0; //atan2(mypos.second - pos.second, mypos.first - pos.first);
								decltype(pos) newmobpos = std::make_pair(mypos.first, mypos.second);
								//x
								newmobpos.first += DISTANCE_CLOSE * std::cos(degree);
								newmobpos.second += DISTANCE_CLOSE * std::sin(degree);
								mp->move(newmobpos.first, newmobpos.second);
								mp->setRealWaitingTime(timeNow, 100);// 0.1 sec w8
							}
							else if (distance > DISTANCE_MAX_TILL_CIRCLE)
							{
								double degree = random<uint32_t>(360) * M_PI / 180.0;
								auto pos = mp->getPosition();
								auto mypos = Position_t(userIfTriggered->getX(), userIfTriggered->getY());
								decltype(pos) newmobpos = std::make_pair(mypos.first, mypos.second);
								//x
								newmobpos.first += DISTANCE_CLOSE * std::cos(degree);
								newmobpos.second += DISTANCE_CLOSE * std::sin(degree);
								mp->move(newmobpos.first, newmobpos.second);
								mp->setRealWaitingTime(timeNow, 1500);// 0.1 sec w8
							}
							else if (distance < DISTANCE_FOLLOW * 1.5)
							{
								//* TOO CLOSE * /
								auto pos = mp->getPosition();
								auto mypos = Position_t(userIfTriggered->getX(), userIfTriggered->getY());
								unsigned int degree = random<uint32_t>(360) * M_PI / 180.0;
								decltype(pos) newmobpos = std::make_pair(mypos.first, mypos.second);
								//x
								newmobpos.first += DISTANCE_CLOSE * std::cos(degree);
								newmobpos.second += DISTANCE_CLOSE * std::sin(degree);
								mp->move(newmobpos.first, newmobpos.second);
								mp->setRealWaitingTime(timeNow, 1500);// 0.1 sec w8
							}
							else
							{
								//* MOVEMENT IN RANGE * /
								mp->setRealWaitingTime(timeNow, 1500);
							}
						}
						else
						{
							//enemy "misteroiusly" disappeared im not gonna fix this typo its 5 am let me be
							triggeredId = 0;
							mp->setFocusToPlayer(0);
						}
					}
					else{
						//handle "roaming"
						/* TODO: FIX ON RADIATION ZONE */
						int randx = 0;
						int randy = 0;
						const int RANGE = 10000;
						do {
							randx = random<pos_t>(-RANGE / 2, RANGE / 2) + p.first;
							randy = random<pos_t>(-RANGE / 2, RANGE / 2) + p.second;
						} while (randx > session.getMap().getWidth() || randy > session.getMap().getHeight() || randx < 0 || randy < 0);

						mp->move(randx, randy);
						mp->generateRandomWaitingTime(timeNow,2500, 15000);

					}
				}
			}
		}//*/
		session.unlockMobsRead();
	}
}

void CSessionsManager::secondTick()
{


	std::vector<CSession>::iterator permSessionIt = m_permSessions.begin();
	std::vector<CSession>::iterator tempSessionIt = m_tempSessions.begin();

	std::vector<handlePtr> goingToDiePlayers; // this hurts
	for(;;)
	{
		std::vector<CSession>::iterator sessionIt;
		if (permSessionIt != m_permSessions.end())
		{
			sessionIt = permSessionIt;
			++permSessionIt;
		}
		else
		{
			/* permIterator is done iterate throguh temp */
			if (tempSessionIt != m_tempSessions.end())
			{
				sessionIt = tempSessionIt;
				++tempSessionIt;
			}
			else
			{
				/* tempIterator is done aswell */
				break;
			}
		}
		CSession& session = sessionIt.operator*();
		auto timeNow = getTimeNow();
		/* Connections */
		session.lockConnectionsRead();
		for (auto player_pair : session.getAllConnections())
		{
			handlePtr& player = player_pair.second; //copy shared ptr for ref count
			if (player)
			{
				player->checkForObjectsToInteract();
				DBUtil::funcs::setPos(player->getID(), player->getPos());

				//radiation zone check
				bool isInRad = player->isInRadiationzone();
				int secondsInRad = player->getSecondsInRadiationzone();
				if (isInRad || secondsInRad) // we need to refresh it back to 0 if player is outside
				{
					if (!isInRad)
						secondsInRad = 0;
					else
					{
						player->setSecondsInRadiationzone(++secondsInRad);
						// damage is relative to distance in radiation zone
						Position_t pos = player->getPos();
						//signed < unsigned is not a beauty
						pos_t mapWidth = static_cast<pos_t>(session.getMap().getWidth());
						pos_t mapHeight = static_cast<pos_t>(session.getMap().getHeight());
						pos_t dx = 0,dy = 0;

						if (pos.first < 0 || pos.first > mapWidth)
							dx = pos.first - (pos.first > mapWidth ? mapWidth : 0);
						if (pos.second < 0 || pos.second > mapHeight)
							dy = pos.second - (pos.second > mapHeight ? mapHeight : 0);
						double distance = std::sqrt(dx * dx + dy * dy);

						health_t maxHP = player->getMaxHP();
						damage_t dmg = 0;

						if (distance > CMap::RADIATIONZONE_DISTANCE_STRONG)
							dmg = maxHP * 0.10;
						else if (distance > CMap::RADIATIONZONE_DISTANCE_MEDIUM)
							dmg = maxHP * 0.05;
						else if (distance > CMap::RADIATIONZONE_DISTANCE_WEAK)
							dmg = maxHP * 0.01;

						bool dead = player->receiveDamageHP(dmg) < 0;
						// i am unhappy with this solution
						if (dead)
						{
							goingToDiePlayers.push_back(player);
							continue; // dead, we dont care about the other ifs
						}
						else
							player->updateHealth(dmg); // make a bubble function?
						
					}
				}
				if (timeHasPassed(player->getShieldPreventTime(),5000))
				{
					//* Handle Shield regen* /
					shield_t sr = player->getShieldRegen();
					player->addSHD(sr);
					//* Handle Repair* /
					if (timeHasPassed(player->getRepairPreventTime(), 2000))
					{
						if (player->isRepairing())
						{
							health_t beforeHeal = player->getHP();

							health_t give = player->getRepBotHPRegen();

							health_t received = player->addHP(give);
							if (beforeHeal + received >= player->getMaxHP())
							{
								player->setRepairing(false);
							}
						}
					}
				}

			}
			else
			{
				dcout << "Player is nullptr this is a major crime call 9-1-1" << cendl;
			}
		}
		if (goingToDiePlayers.size() != 0)
		{
			dcout << "WAiting for input on death" << cendl;
			std::cin.get();
		}
		session.unlockConnectionsRead();
		//I did put effort that deleting a player twice cannot occur, but you know there will be cases, so the worst case that should have been taken care of by CSession should be:
		// 1. Player dies by radiation zone
		// 2. before the program reaches this for loop somebody kills the player in an attacker thread
		// 3. the player is removed from connectionTable ( shared_ptr from goingToDiePlayers keeps player data "alive", but hes removed from the table nontheless)
		// 4. the player will not get removed again in this for loop (?????)
		// TODO: TEST THIS BY BREAKPOINTING BEFORE DIE()
		for (auto& players : goingToDiePlayers)
		{
			players->die();
		}
		goingToDiePlayers.clear();


		/* Collectable tick includes delete operation */
		session.lockCollectablesRead();
		for (auto it = session.getCollectables().begin(); it != session.getCollectables().end();)
		{
			std::shared_ptr<ICollectable>& collectable = (*it).second;
			if (collectable && (collectable->getType() == CResourceBox::RESOURCE_BOX_DEFAULT_COLLECTABLE_ID ||
				collectable->getType() == CResourceBox::RESOURCE_BOX_PRIVATE_COLLECTABLE_ID))
			{
				std::shared_ptr<CResourceBox> res = std::static_pointer_cast<CResourceBox>(collectable);
				if (res->boxIsPrivate())
				{
					res->m_belongsToSeconds--;
					if (res->m_belongsToSeconds == 0)
					{
						/* Unprivate */
						res->onChangeToPublic();
					}
				}
				if (res->m_existsSeconds-- == 0) // yes it goes to -1 but then its precise and the time is correct
				{
					/* Unlock read to write operates then put it back to lock read*/
					session.unlockCollectablesRead();
					it = res->remove();
					session.lockCollectablesRead();
				}
				else
				{
					it++;
				}
			}
			else
			{
				it++;
			}
		}
		session.unlockCollectablesRead();


		session.lockMobsRead();
		//this segment will NOT insert delete the container
		for (auto mobp : session.getMobs())
		{
			std::shared_ptr<CMob>& mp = mobp.second;
			if (mp == nullptr) continue;

			if (mp->getFocusedPlayer() > 0 && mp->getFocusedPlayer() < BEGIN_MOB_IDS &&
				mp->attacking())
			{
				auto player = session.getHandler(mp->getFocusedPlayer());
				if (player) {
					Position_t alienpos = mp->getPosition();
					Position_t userpos =
						std::make_pair(player->getX(),
							player->getY());
					pos_t x = alienpos.first - userpos.first;
					pos_t y = alienpos.second - userpos.second;
					auto distance = std::sqrt(x * x + y * y);
					if (distance < Constants::Game::FIGHT_RANGE_NPC)
					{
						/* IN RANGE */
						damage_t damage = m_dm.damageLaser(mp->getId(), 1, mp->getShip().dmg, false);

						//player->sendPacket(pm.damageBubbleSelf()
						if (player->receiveDamagePure(damage) >= 0)
						{
							/* Player is alive and healthy and is able to get attacked */
							mp->attack(mp->getFocusedPlayer());
						}
						else
						{
							/* Player is ded */
							player->die();
							mp->abort();
							mp->setLastTimeShotByBelongedPlayer(0LL);
							mp->setFocusToPlayer(0);
						}

					}
					else
					{
						//no player founderino
						//dcout << "out of range but its notout of range but something i wrote too late in the night" << cendl;

						//stop the lasers. I have yet to find the packet that keeps "Locking" on a player but doesnt shoot lasers/whatever on him
						mp->abort();
						//mp->setLastTimeShotByFirstPlayer(0LL);
						//mp->setTriggerPerson(0);
					}
				}
			}
			if (mp->isGreyed() && mp->getLastTimeShotByBelongedPlayer() + 10000 < timeNow) // iterated 4000 times gettimenow() in fast tick and wonder why this function took so much time 
			{

				dcout << "Ungrey opponent: " << cendl;
				mp->setLastTimeShotByBelongedPlayer(0); //* Kinda unncessary but who cares * /
				mp->ungrey();
				session.sendEveryone(m_pm.ungreyOpponent(mp->getId()));
			}
			if (timeHasPassed(timeNow, mp->getLastTimeShot(), 10000))
			{
				//TODO SetHP SetSHD
				mp->increaseSHD(mp->getShip().shdmax * 0.05);
			}
		}
		session.unlockMobsRead();
	}
}
