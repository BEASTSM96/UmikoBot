#include "CurrencyModule.h"
#include "UmikoBot.h"
#include "core/Permissions.h"
#include "core/Utility.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/qregexp.h>
#include <QtCore/QtMath>

#include <random>

//! Currency Config Location
#define currenConfigLoc QString("currencyConfig")

//! Maximum amount that can be bet
#define gamblebetMax 100

//! Maximum debt that a user can be in
#define debtMax -100

//! Gamble Timeout in seconds
#define gambleTimeout 20

CurrencyModule::CurrencyModule(UmikoBot* client) : Module("currency", true), m_client(client)
{

	m_timer.setInterval(24*60*60*1000); //!24hr timer
	QObject::connect(&m_timer, &QTimer::timeout, [this, client]() 
		{
			for (auto server : guildList.keys()) 
			{
				//!Reset the daily claimed bool for everyone
				for (int i = 0; i < guildList[server].size(); i++) 
				{
					auto& user = guildList[server][i];
					
					if (!user.isDailyClaimed)
					{
						user.dailyStreak = 0;
					}

					user.isDailyClaimed = false;

					if (UmikoBot::Instance().GetName(server, user.userId) != "") 
					{
						user.isDailyClaimed = false;
					}
					else 
					{
						//remove if the user is no longer in the server
						guildList[server].removeAt(i);
					}
				}
				auto guildId = server;
				auto& serverConfig = getServerData(guildId);

				if (!serverConfig.isRandomGiveawayDone) 
				{
					client->createMessage(serverConfig.giveawayChannelId, "Hey everyone! Today's freebie expires in **" + QString::number(serverConfig.freebieExpireTime) + " seconds**. `!claim` it now!");

					serverConfig.allowGiveaway = true;
					
					//! Delete the previously allocated thingy
					if (serverConfig.freebieTimer != nullptr) {
						delete serverConfig.freebieTimer;
						serverConfig.freebieTimer = nullptr;
					}

					serverConfig.freebieTimer = new QTimer;
					serverConfig.freebieTimer->setInterval(serverConfig.freebieExpireTime * 1000);
					serverConfig.freebieTimer->setSingleShot(true);
					QObject::connect(serverConfig.freebieTimer, &QTimer::timeout, [this, client, guildId] ()
						{
							auto& serverConfig = getServerData(guildId);
							serverConfig.allowGiveaway = false;
							serverConfig.giveawayClaimer = 0;
						});
					serverConfig.freebieTimer->start();
				}
				serverConfig.isRandomGiveawayDone = false;
				serverConfig.giveawayClaimer = 0;
			}
	});

	m_timer.start();

	RegisterCommand(Commands::CURRENCY_WALLET, "wallet", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		QStringList args = message.content().split(' ');

		if (args.size() > 2) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}

		snowflake_t user;
		QList<Discord::User> mentions = message.mentions();

		if (mentions.size() > 0)
		{
			user = mentions[0].id();
		}
		else
		{
			if (args.size() == 2)
			{
				user = UmikoBot::Instance().GetUserFromArg(channel.guildId(), args, 1);

				if (user == 0)
				{
					client.createMessage(message.channelId(), "**Couldn't find " + args.at(1) + "**");
					return;
				}
			}
			else
			{
				user = message.author().id();
			}
		}

		UmikoBot::Instance().GetAvatar(channel.guildId(), user).then(
			[this, user, channel, &client, message](const QString& icon)
		{
			//! Get User and Sever Data
			auto guild = channel.guildId();
			auto config = getServerData(guild);

			Discord::Embed embed;
			embed.setColor(qrand() % 11777216);
			embed.setAuthor(Discord::EmbedAuthor(UmikoBot::Instance().GetName(channel.guildId(), user) + "'s Wallet", "", icon));

			QString credits = QString::number(getUserData(guild, user).currency());
			QString dailyStreak = QString::number(getUserData(guild, user).dailyStreak);
			QString dailyClaimed = getUserData(guild, user).isDailyClaimed ? "Yes" : "No";

			QString desc = "Current Credits: **" + credits + " " + config.currencySymbol + "** (" + config.currencyName + ")";
			desc += "\n";
			desc += "Daily Streak: **" + dailyStreak + "/" + QString::number(config.dailyBonusPeriod) + "**\n";
			desc += "Today's Daily Claimed? **" + dailyClaimed + "**";
			embed.setDescription(desc);
			client.createMessage(message.channelId(), embed);
		});
	});

	RegisterCommand(Commands::CURRENCY_DAILY, "daily", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');
		auto& config = getServerData(channel.guildId());

		if (args.size() > 1) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}

		int jailRemainingTime = guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].jailTimer->remainingTime();
		
		if (getUserData(channel.guildId(), message.author().id()).isDailyClaimed)
		{
			QString time = utility::StringifyMilliseconds(m_timer.remainingTime());
			QString desc = "**You have already claimed your daily credits.**\nCome back after `" + time + "` to get more rich!";

			client.createMessage(message.channelId(), desc);
		}
		else if (jailRemainingTime > 0)
		{
			QString time = utility::StringifyMilliseconds(jailRemainingTime);
			QString desc = "**You are in jail!**\nCome back after " + time + " to collect your daily credits.";
			client.createMessage(message.channelId(), desc);
			return;
		}
		else 
		{
			auto index = getUserIndex(channel.guildId(), message.author().id());
			unsigned int& dailyStreak = guildList[channel.guildId()][index].dailyStreak;
			double todaysReward = config.dailyReward;
			bool bonus = false;

			if (++dailyStreak % config.dailyBonusPeriod == 0)
			{
				todaysReward += config.dailyBonusAmount;
				bonus = true;
			}

			guildList[channel.guildId()][index].isDailyClaimed = true;
			guildList[channel.guildId()][index].setCurrency(guildList[channel.guildId()][index].currency() + todaysReward);
			guildList[channel.guildId()][index].numberOfDailysClaimed += 1;
			QString displayedMessage = "";

			if (bonus)
			{
				displayedMessage += "**Bonus!** ";
			}

			displayedMessage += "You now have **" + QString::number(todaysReward) + "** more " + getServerData(channel.guildId()).currencyName + "s in your wallet!\n";
			displayedMessage += "Streak: **" + QString::number(dailyStreak) + "/" + QString::number(config.dailyBonusPeriod) + "**";

			if (dailyStreak % config.dailyBonusPeriod == 0)
			{
				dailyStreak = 0;
			}

			client.createMessage(message.channelId(), displayedMessage);
		}

	});

	RegisterCommand(Commands::CURRENCY_GAMBLE, "gamble", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		
		QStringList args = message.content().split(' ');
		
		if (args.size() > 2) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}

		int jailRemainingTime = guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].jailTimer->remainingTime();
		if (jailRemainingTime > 0)
		{
			QString time = utility::StringifyMilliseconds(jailRemainingTime);
			QString desc = "**You are in jail!**\nCome back after " + time + " to gamble.";
			client.createMessage(message.channelId(), desc);
			return;
		}
		
		//! Normal Mode
		if (args.size() == 1)
		{

			auto& serverGamble = gambleData[channel.guildId()];
			if (guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].currency() - getServerData(channel.guildId()).gambleLoss < debtMax)
			{
				client.createMessage(message.channelId(), "**Nope, can't let you get to serious debt.**");
				return;
			}
			if (serverGamble.gamble) 
			{
				QString user = UmikoBot::Instance().GetName(channel.guildId(), serverGamble.userId);
				Discord::Embed embed;
				embed.setColor(qrand() % 16777216);
				embed.setTitle("Welcome to Gamble!");
				embed.setDescription("Sorry but this feature is currently in use by **" + user + "**. Please try again later!");
				client.createMessage(message.channelId(), embed);
				return;
			}

			auto& config = getServerData(channel.guildId());

			std::random_device device;
			std::mt19937 rng(device());
			std::uniform_int_distribution<std::mt19937::result_type> dist(config.minGuess, config.maxGuess);

			serverGamble.randNum = dist(rng);
			serverGamble.channelId = message.channelId();
			serverGamble.gamble = true;
			serverGamble.userId = message.author().id();

			auto guild = channel.guildId();

			serverGamble.timer = new QTimer();

			serverGamble.timer->setInterval(gambleTimeout * 1000);
			serverGamble.timer->setSingleShot(true);
			QObject::connect(serverGamble.timer, &QTimer::timeout, [this, &client, guild, message]() 
				{
				if (gambleData[guild].gamble || gambleData[guild].doubleOrNothing) 
				{
					gambleData[guild].gamble = false;
					gambleData[guild].doubleOrNothing = false;

					client.createMessage(message.channelId(), "**Gamble Timeout Due to No Valid Response**");

				}				
				});

			serverGamble.timer->start();

			QString name = UmikoBot::Instance().GetName(channel.guildId(), serverGamble.userId);

			Discord::Embed embed;
			embed.setColor(qrand() % 16777216);
			embed.setTitle("Welcome to Gamble " + name + "!");
			embed.setDescription("All you need to do is guess a random number between " + QString::number(config.minGuess) + " and " + QString::number(config.maxGuess) + " (inclusive) and if it is the same as the number I think of, you get **" + QString::number(config.gambleReward) + config.currencySymbol + "**!\n\n**What number do you think of?** " + utility::consts::emojis::WE_SMART);
			
			client.createMessage(message.channelId(), embed);
		}

		//! Double or Nothing
		if (args.size() == 2)
		{
			QRegExp re("[+]?\\d*\\.?\\d+");
			if (!re.exactMatch(args.at(1))) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** The argument must be a **positive number**.");
					return;
			}

			auto& serverGamble = gambleData[channel.guildId()];
			auto& config = getServerData(channel.guildId());

			if (guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].currency() - args.at(1).toDouble() < debtMax)
			{
				client.createMessage(message.channelId(), "**Nope, can't let you get to serious debt.**");
				return;
			}
			if (serverGamble.gamble)
			{
				QString user = UmikoBot::Instance().GetName(channel.guildId(), serverGamble.userId);
				Discord::Embed embed;
				embed.setColor(qrand() % 16777216);
				embed.setTitle("Welcome to Gamble!");
				embed.setDescription("Sorry but this feature is currently in use by **" + user + "**. Please try again later!");
				client.createMessage(message.channelId(), embed);
				return;
			}
			if (args.at(1).toDouble() > gamblebetMax) 
			{
				client.createMessage(channel.id(), "You cannot bet an amount more than **" + QString::number(gamblebetMax) + config.currencySymbol+"**");
				return;
			}
			if (args.at(1).toDouble() == 0) 
			{
				client.createMessage(message.channelId(), QString(utility::consts::emojis::AANGER) + " **BRUH. Don't you dare waste my time! I ain't interested in nothing.**");
				return;
			}

			std::random_device device;
			std::mt19937 rng(device());
			std::uniform_int_distribution<std::mt19937::result_type> dist(config.minGuess, config.maxGuess);

			serverGamble.randNum = dist(rng);
			serverGamble.channelId = message.channelId();
			serverGamble.gamble = true;
			serverGamble.userId = message.author().id();
			serverGamble.doubleOrNothing = true;
			serverGamble.betAmount = args.at(1).toDouble();

			auto guild = channel.guildId();

			serverGamble.timer = new QTimer();

			serverGamble.timer->setInterval(gambleTimeout * 1000);
			serverGamble.timer->setSingleShot(true);
			QObject::connect(serverGamble.timer, &QTimer::timeout, [this, &client, guild, message]() 
			{

				if (gambleData[guild].gamble || gambleData[guild].doubleOrNothing) 
				{
					gambleData[guild].gamble = false;
					gambleData[guild].doubleOrNothing = false;

					client.createMessage(message.channelId(), "**Gamble Timeout Due to No Valid Response**");

				}
				
			});

			serverGamble.timer->start();
			QString name = UmikoBot::Instance().GetName(channel.guildId(), serverGamble.userId);

			Discord::Embed embed;
			embed.setColor(qrand() % 16777216);
			embed.setTitle("Welcome to Gamble (Double or Nothing) " + name + "!");
			embed.setDescription("All you need to do is guess a random number between " + QString::number(config.minGuess) + " and " + QString::number(config.maxGuess) + " (inclusive) and if it is the same as the number I guess, you get double the amount you just bet: **" + QString::number(2* serverGamble.betAmount) + config.currencySymbol + "**!\n\n**What number do you think of?** " + utility::consts::emojis::WE_SMART);

			client.createMessage(message.channelId(), embed);
		}

	});

	RegisterCommand(Commands::CURRENCY_CLAIM, "claim", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		if (args.size() > 1) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}

		if (!guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].canClaimFreebies)
		{
			client.createMessage(message.channelId(), "**You can't claim freebies while in safemode!**");
			return;
		}

		if (args.size() == 1) 
		{
			if (!getServerData(channel.guildId()).isRandomGiveawayDone) 
			{
				if (getServerData(channel.guildId()).allowGiveaway)
				{
					int jailRemainingTime = guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].jailTimer->remainingTime();
					if (jailRemainingTime > 0)
					{
						QString time = utility::StringifyMilliseconds(jailRemainingTime);
						QString desc = "**You are in jail!**\nYou can't make claims to freebies for another " + time;
						client.createMessage(message.channelId(), desc);
						return;
					}
					
					auto& config = getServerData(channel.guildId());
					config.giveawayClaimer = message.author().id();
					Discord::Embed embed;
					embed.setColor(qrand() % 11777216);
					embed.setTitle("Claim FREEBIE");
					embed.setDescription(":drum: And today's FREEBIE goes to **" + message.author().username() + "**! \n\n Congratulations! You just got **"+ QString::number(config.freebieReward) + config.currencySymbol +"**!");

					auto index = getUserIndex(channel.guildId(), message.author().id());

					guildList[channel.guildId()][index].setCurrency(guildList[channel.guildId()][index].currency() + config.freebieReward);
					guildList[channel.guildId()][index].numberOfGiveawaysClaimed += 1;

					client.createMessage(message.channelId(), embed);
					getServerData(channel.guildId()).isRandomGiveawayDone = true;
					getServerData(channel.guildId()).allowGiveaway = false;
				}
				else 
				{
					client.createMessage(message.channelId(), "**BRUH**, ***yOu CaN't JuSt GeT fReE sTuFf aNyTiMe.***");
				}
			}
			else 
			{
				auto& config = getServerData(channel.guildId());
				Discord::Embed embed;
				embed.setColor(qrand()%11777216);
				embed.setTitle("Claim FREEBIE");
				embed.setDescription("Sorry, today's freebie has been claimed by " + UmikoBot::Instance().GetName(channel.guildId(), config.giveawayClaimer) + " :cry: \n\n But you can always try again the next day!");
				client.createMessage(message.channelId(), embed);
			}
		}

	});

	RegisterCommand(Commands::CURRENCY_SET_PRIZE_CHANNEL, "setannouncechan", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}
			
			if (args.size() > 1) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 1) 
			{
				auto& config = getServerData(channel.guildId());
				config.giveawayChannelId = message.channelId();
				client.createMessage(message.channelId(), "**Giveaway announcement channel successfully changed to current channel.**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_NAME, "setcurrenname", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		
		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}
			if (args.size() == 1) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.currencyName = args.at(1);
				client.createMessage(message.channelId(), "Currency Name set to **" + config.currencyName + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_SYMBOL, "setcurrensymb", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.currencySymbol = args.at(1);
				client.createMessage(message.channelId(), "Currency Symbol set to **" + config.currencySymbol + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_DAILY, "setdaily", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.dailyReward = args.at(1).toInt();
				client.createMessage(message.channelId(), "Daily Reward Amount set to **" + QString::number(config.dailyReward) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_PRIZE , "setprize", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.freebieReward = args.at(1).toInt();
				client.createMessage(message.channelId(), "Freebie Reward Amount set to **" + QString::number(config.freebieReward) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_PRIZE_PROB, "setprizeprob", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.randGiveawayProb = args.at(1).toDouble();
				client.createMessage(message.channelId(), "Giveaway Probability Amount set to **" + QString::number(config.randGiveawayProb) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_PRIZE_EXPIRY, "setprizeexpiry", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.freebieExpireTime = args.at(1).toUInt();
				client.createMessage(message.channelId(), "Freebie Expiry Time (secs) set to **" + QString::number(config.freebieExpireTime) + "**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_GAMBLE_LOSS, "setgambleloss", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.gambleLoss = args.at(1).toInt();
				client.createMessage(message.channelId(), "Gamble Loss Amount set to **" + QString::number(config.gambleLoss) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_GAMBLE_MAX_GUESS, "setgamblemaxguess", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.maxGuess = args.at(1).toInt();
				client.createMessage(message.channelId(), "Gamble Max Guess set to **" + QString::number(config.maxGuess) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_GAMBLE_MIN_GUESS, "setgambleminguess", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		
		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.minGuess = args.at(1).toInt();
				client.createMessage(message.channelId(), "Gamble Min Guess set to **" + QString::number(config.minGuess) + "**");
			}
		});
	});

	RegisterCommand(Commands::CURRENCY_SET_GAMBLE_REWARD, "setgamblereward", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2) 
			{
				auto& config = getServerData(channel.guildId());
				config.gambleReward = args.at(1).toInt();
				client.createMessage(message.channelId(), "Gamble Reward Amount set to **" + QString::number(config.gambleReward) + "**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_RICH_LIST, "richlist", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{
		
		QStringList args = message.content().split(' ');

		if (args.size() > 1) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}
		else 
		{
			//! Print the top 30 (or less depending on number of 
			//! members) people in the leaderboard

			auto& leaderboard = guildList[channel.guildId()];

			int offset{ 30 };
			if (leaderboard.size() < 30) 
				{
					offset = leaderboard.size();
				}

			qSort(leaderboard.begin(), leaderboard.end(), [](UserCurrency u1, UserCurrency u2)
					{
						return u1.currency() > u2.currency();
					});

			Discord::Embed embed;
			embed.setTitle("Currency Leaderboard (Top 30)");
			QString desc;
			int rank = 0;
			int numberOfDigits = QString::number(offset).size();

			for (auto i = 0; i < offset && i < leaderboard.size(); i++) 
			{
				const auto& user = leaderboard[i];
				QString username = UmikoBot::Instance().GetName(channel.guildId(), user.userId);

				if (username == "") 
				{
					offset++;
					continue;
				}

				rank++;

				QString currency = QString::number(user.currency());

				desc += "`" + QString::number(rank).rightJustified(numberOfDigits, ' ') + "`) **" + username + "** - ";
				desc += currency + " " + getServerData(channel.guildId()).currencySymbol + "\n";
			}

			embed.setColor(qrand() % 16777216);
			embed.setDescription(desc);

			client.createMessage(message.channelId(), embed);
		}
		
	});

	RegisterCommand(Commands::CURRENCY_DONATE, "donate", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		if (args.size() == 1 || args.size() == 2) 
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
			return;
		}
		QRegExp re("[+]?\\d*\\.?\\d+");
		if (!re.exactMatch(args.at(1))) 
		{
				client.createMessage(message.channelId(), "**You can't donate in invalid amounts**");
				return;
		}

		if (args.at(1).toDouble() == 0) 
		{
			client.createMessage(message.channelId(), "**Please don't donate at all if you don't want to donate anything.**");
			return;
		}

		if (guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())].currency() - args.at(1).toDouble() < 0)
		{
			client.createMessage(message.channelId(), "**I can't let you do that, otherwise you'll be in debt!**");
			return;
		}

		QList<Discord::User> mentions = message.mentions();
		if (mentions.size() == 0) 
		{
			client.createMessage(message.channelId(), "**Who do you want to donate to? Please `@` all those people.**");
			return;
		}

		for (auto user : mentions) 
		{
			if (user.id() == message.author().id()) 
			{
				client.createMessage(message.channelId(), "**You cannot donate to yourself!**\nPlease remove yourself from the list.");
				return;
			}
		}

		auto& userCurrency = guildList[channel.guildId()][getUserIndex(channel.guildId(), message.author().id())];
		userCurrency.setCurrency(userCurrency.currency() - args.at(1).toDouble());

		double donation = args.at(1).toDouble() / mentions.size();
		QString desc = "<@" + QString::number(message.author().id()) + "> donated **" + QString::number(donation) + getServerData(channel.guildId()).currencySymbol + "** to";

		for (auto user : mentions) 
		{
			auto index = getUserIndex(channel.guildId(), user.id());
			desc += " <@" + QString::number(user.id()) + ">";
			guildList[channel.guildId()][index].setCurrency(guildList[channel.guildId()][index].currency() + donation);
		}

		Discord::Embed embed;
		embed.setTitle("Donation by " + UmikoBot::Instance().GetName(channel.guildId(), message.author().id()) + "!");
		embed.setDescription(desc);
		embed.setColor(qrand() % 16777216);

		client.createMessage(message.channelId(), embed);

	});
	RegisterCommand(Commands::CURRENCY_BRIBE, "bribe", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel)
	{

		QStringList args = message.content().split(' ');
		auto& config = getServerData(channel.guildId());
		snowflake_t authorId = message.author().id();

		auto& authorCurrency = guildList[channel.guildId()][getUserIndex(channel.guildId(), authorId)];
		int remainingJailTime = authorCurrency.jailTimer->remainingTime();
		bool inJail = authorCurrency.jailTimer->isActive();


		if (inJail == false)
		{
			QString output = QString(
				":police_officer: **Hey you're not in JAIL!** :police_officer:\n"
			);
			client.createMessage(message.channelId(), output);
			return;
		}

		if (args.size() != 2)
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!**");
			return;
		}

		QRegExp re("[+]?\\d*\\.?\\d+");
		if (!re.exactMatch(args.at(1)))
		{
			client.createMessage(message.channelId(), "**Your bribe amount must be valid!**");
			return;
		}

		double amountToBribe = args.at(1).toDouble();

		auto guildID = channel.guildId();
		QObject::connect(authorCurrency.jailTimer, &QTimer::timeout, [this, &client, guildID, authorId]()
			{
				guildList[guildID][getUserIndex(guildID, authorId)].isBribeUsed = false;
			});

		if (authorCurrency.isBribeUsed)
		{
			QString output = QString(
				":police_officer: **You already tried to bribe me and failed... Do you want me to extend your sentence again?** :police_officer:\n"
			);

			client.createMessage(message.channelId(), output);
			return;
		}

		else if (amountToBribe > config.bribeMaxAmount)
		{
			QString maxBribeAmount = QString::number(config.bribeMaxAmount);
			QString output = QString(
				":police_officer: **If I take more than `%1` %2 then I might get caught... ** :police_officer:\n"
			).arg(maxBribeAmount, config.currencySymbol);

			client.createMessage(message.channelId(), output);
			return;
		}

		else if (amountToBribe < config.bribeLeastAmount)
		{
			QString leastBribeAmount = QString::number(config.bribeLeastAmount);
			QString output = QString(
				":police_officer: **Pfft! Such measly amounts... Do you want to be in jail for longer?** :police_officer:\n"
				"*(You can always give me `%1 %2` or more... maybe then I could do something?)*\n"
			).arg(leastBribeAmount, config.currencySymbol);

			client.createMessage(message.channelId(), output);
			return;
		}

		if (authorCurrency.currency() - (amountToBribe) < debtMax)
		{
			client.createMessage(message.channelId(), ":police_officer: **I would love to be bribed with that much money but unfortunately you can't afford it.** :police_officer:");
			return;
		}

		// This success chance is in the range of 0 to 1
		double successChance = (static_cast<double>(config.bribeSuccessChance) / (static_cast<double>(config.bribeMaxAmount) * 100)) * amountToBribe;

		QString authorName = UmikoBot::Instance().GetName(channel.guildId(), authorId);

		std::random_device device;
		std::mt19937 prng{ device() };
		std::discrete_distribution<> distribution{ { 1 - successChance, successChance } };
		int roll = distribution(prng);

		if (roll)
		{
			authorCurrency.setCurrency(authorCurrency.currency() - amountToBribe);
			authorCurrency.jailTimer->stop();

			QString output = QString(
				":unlock: **Thanks for the BRIBE!** :unlock:\n"
				"*%1* you are free from jail now!.\n"
			).arg(authorName);

			client.createMessage(message.channelId(), output);

			snowflake_t chan = message.channelId();
			UmikoBot::Instance().createReaction(chan, message.id(), utility::consts::emojis::reacts::PARTY_CAT);
		}
		else
		{
			authorCurrency.isBribeUsed = true;
			int totalTime = remainingJailTime + 3600000;
			authorCurrency.jailTimer->start(totalTime);

			QString time = utility::StringifyMilliseconds(totalTime);

			QString output = QString(
				":police_officer: **Your bribes don't affect my loyalty!** :police_officer:\n"
				"You have been reported and your sentence has been extended by `1` hour!.\n"
				"*(You need to wait %1 to get out of jail)*"
			).arg(time);
			client.createMessage(message.channelId(), output);
		}

	});
	RegisterCommand(Commands::CURRENCY_SET_BRIBE_SUCCESS_CHANCE, "setbribesuccesschance", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel)
		{

			QStringList args = message.content().split(' ');

			Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
				[this, args, &client, message, channel](bool result)
				{
					GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
					if (!result)
					{
						client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
						return;
					}

					if (args.size() == 1 || args.size() > 2)
					{
						client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
						return;
					}

					if (args.size() == 2)
					{
						auto& config = getServerData(channel.guildId());
						config.bribeSuccessChance = args.at(1).toInt();
						client.createMessage(message.channelId(), "Bribe Success Chance set to **" + QString::number(config.bribeSuccessChance) + "%**");
					}

				});
		});
	RegisterCommand(Commands::CURRENCY_SET_MAX_BRIBE_AMOUNT, "setmaxbribeamount", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel)
		{

			QStringList args = message.content().split(' ');

			Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
				[this, args, &client, message, channel](bool result)
				{
					GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
					if (!result)
					{
						client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
						return;
					}

					if (args.size() == 1 || args.size() > 2)
					{
						client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
						return;
					}

					if (args.size() == 2)
					{
						auto& config = getServerData(channel.guildId());
						config.bribeMaxAmount = args.at(1).toInt();
						client.createMessage(message.channelId(), "Bribe Max Amount set to **" + QString::number(config.bribeMaxAmount) + "**");
					}

				});
		});
	RegisterCommand(Commands::CURRENCY_SET_LEAST_BRIBE_AMOUNT, "setleastbribeamount", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel)
		{

			QStringList args = message.content().split(' ');

			Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
				[this, args, &client, message, channel](bool result)
				{
					GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
					if (!result)
					{
						client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
						return;
					}

					if (args.size() == 1 || args.size() > 2)
					{
						client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
						return;
					}

					if (args.size() == 2)
					{
						auto& config = getServerData(channel.guildId());
						config.bribeLeastAmount = args.at(1).toInt();
						client.createMessage(message.channelId(), "Bribe Least Amount set to **" + QString::number(config.bribeLeastAmount) + "**");
					}

				});
		});
	RegisterCommand(Commands::CURRENCY_STEAL, "steal", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel)
	{

		QStringList args = message.content().split(' ');
		auto& config = getServerData(channel.guildId());
		snowflake_t authorId = message.author().id();

		if (args.size() != 3)
		{
			client.createMessage(message.channelId(), "**Wrong Usage of Command!**");
			return;
		}

		CurrencyModule* currencyModule = static_cast<CurrencyModule*>(UmikoBot::Instance().GetModuleByName("currency"));
		if (currencyModule)
		{
			const CurrencyModule::UserCurrency& userCurrency = currencyModule->getUserData(channel.guildId(), authorId);
			if (userCurrency.canSteal == false)
			{
				client.createMessage(message.channelId(), "**You can't steal while in safemode!**");
				return;
			}
		}
		
		CurrencyModule* currencyModule = static_cast<CurrencyModule*>(UmikoBot::Instance().GetModuleByName("currency"));
		if (currencyModule)
		{
			const CurrencyModule::UserCurrency& userCurrency = currencyModule->getUserData(channel.guildId(), authorId);
			if (userCurrency.canSteal == false)
			{
				client.createMessage(message.channelId(), "**You can't steal while in safemode!**");
				return;
			}
		}

		int jailRemainingTime = guildList[channel.guildId()][getUserIndex(channel.guildId(), authorId)].jailTimer->remainingTime();
		if (jailRemainingTime > 0)
		{
			QString time = utility::StringifyMilliseconds(jailRemainingTime);
			QString desc = "**You are in jail!**\nCome back after " + time + " to steal more.";
			client.createMessage(message.channelId(), desc);
			return;
		}

		QRegExp re("[+]?\\d*\\.?\\d+");
		if (!re.exactMatch(args.at(1)))
		{
			client.createMessage(message.channelId(), "**You can't steal invalid amounts**");
			return;
		}

		double amountToSteal = args.at(1).toDouble();

		if (amountToSteal == 0.0)
		{
			client.createMessage(message.channelId(), "**Don't bother me if you don't want to steal anything.**");
			return;
		}

		if (guildList[channel.guildId()][getUserIndex(channel.guildId(), authorId)].currency() - (amountToSteal * config.stealFinePercent / 100) < debtMax)
		{
			client.createMessage(message.channelId(), "**I can't let you do that, you might go into serious debt.**");
			return;
		}

		QList<Discord::User> mentions = message.mentions();
		if (mentions.size() == 0)
		{
			client.createMessage(message.channelId(), "**Who do you want to steal from? Please `@` that person.**");
			return;
		}
		else if (mentions.size() > 1)
		{
			client.createMessage(message.channelId(), "**You can only steal from one person at a time.**");
			return;
		}

		snowflake_t victimId = mentions[0].id();
		
		if (guildList[channel.guildId()][getUserIndex(channel.guildId(), victimId)].isInSafemode())
		{
			client.createMessage(message.channelId(), "**The victim is in safemode! **");
			return;
		}
		
				if (guildList[channel.guildId()][getUserIndex(channel.guildId(), victimId)].isInSafemode())
		{
			client.createMessage(message.channelId(), "**The victim is in safemode! **");
			return;
		}

		if (victimId == authorId)
		{
			client.createMessage(message.channelId(), "**You cannot steal from yourself.**");
			return;
		}

		if (guildList[channel.guildId()][getUserIndex(channel.guildId(), victimId)].currency() - amountToSteal < debtMax)
		{
			client.createMessage(message.channelId(), "**I can't let you make your victim go into serious debt.**");
			return;
		}

		// https://www.desmos.com/calculator/z6b0k7wb1t
		// This success chance is in the range of 0 to 1
		double successChance = (config.stealSuccessChance / 100.0) * qExp(-0.0001 * qPow(amountToSteal, 1.5));
			
		QString thiefName = UmikoBot::Instance().GetName(channel.guildId(), authorId);
		QString victimName = UmikoBot::Instance().GetName(channel.guildId(), victimId);
		
		std::random_device device;
		std::mt19937 prng { device() };
		std::discrete_distribution<> distribution { { 1 - successChance, successChance } };
		int roll = distribution(prng);
			
		auto& victimCurrency = guildList[channel.guildId()][getUserIndex(channel.guildId(), victimId)];
		auto& authorCurrency = guildList[channel.guildId()][getUserIndex(channel.guildId(), authorId)];

		if (roll)
		{
			victimCurrency.setCurrency(victimCurrency.currency() - amountToSteal);
			authorCurrency.setCurrency(authorCurrency.currency() + amountToSteal);
				
			QString stealAmount = QString::number(amountToSteal);
			QString output = QString(
					":man_detective: **Steal success!** :man_detective:\n"
					"*%1* has discreetly stolen **`%2 %3`** from under *%4's* nose.\n"
				).arg(thiefName, stealAmount, config.currencySymbol, victimName);
				
			client.createMessage(message.channelId(), output);
		}
		else
		{
			authorCurrency.setCurrency(authorCurrency.currency() - amountToSteal * config.stealFinePercent / 100);
			victimCurrency.setCurrency(victimCurrency.currency() + amountToSteal * config.stealVictimBonusPercent / 100);
			authorCurrency.jailTimer->start(config.stealFailedJailTime * 60 * 60 * 1000);

			QString fineAmount = QString::number(amountToSteal * config.stealFinePercent / 100.0);
			QString victimBonus = QString::number(amountToSteal * config.stealVictimBonusPercent / 100.0);
			QString jailTime = QString::number(config.stealFailedJailTime);
			QString output = QString(
					":rotating_light: **You got caught!** :rotating_light:\n"
					"*%1* has been fined **`%2 %3`** and placed in jail for %4 hours.\n"
					"*%5* has been granted **`%6 %3`** in insurance."
				).arg(thiefName, fineAmount, config.currencySymbol, jailTime, victimName, victimBonus);
				
			client.createMessage(message.channelId(), output);
		}

	});

	RegisterCommand(Commands::CURRENCY_SET_STEAL_SUCCESS_CHANCE, "setstealsuccesschance", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.stealSuccessChance = args.at(1).toInt();
				client.createMessage(message.channelId(), "Steal Success Chance set to **" + QString::number(config.stealSuccessChance) + "%**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_STEAL_FINE_PERCENT, "setstealfine", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.stealFinePercent = args.at(1).toInt();
				client.createMessage(message.channelId(), "Steal Fine Amount set to **" + QString::number(config.stealFinePercent) + "%**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_STEAL_VICTIM_BONUS, "setstealvictimbonus", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.stealVictimBonusPercent = args.at(1).toInt();
				client.createMessage(message.channelId(), "Steal Victim Bonus set to **" + QString::number(config.stealVictimBonusPercent) + "%**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_STEAL_JAIL_HOURS, "setstealjailhours", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.stealFailedJailTime = args.at(1).toInt();
				client.createMessage(message.channelId(), "Steal Jail Time set to **" + QString::number(config.stealFailedJailTime) + " hours**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_DAILY_BONUS_AMOUNT, "setdailybonus", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.dailyBonusAmount = args.at(1).toInt();
				client.createMessage(message.channelId(), "Daily Bonus Amount set to **" + QString::number(config.dailyBonusAmount) + "**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_SET_DAILY_BONUS_PERIOD, "setdailybonusperiod", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			GuildSetting* setting = &GuildSettings::GetGuildSetting(channel.guildId());
			if (!result)
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() == 1 || args.size() > 2)
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			if (args.size() == 2)
			{
				auto& config = getServerData(channel.guildId());
				config.dailyBonusPeriod = args.at(1).toInt();
				client.createMessage(message.channelId(), "Daily Bonus will occur every **" + QString::number(config.dailyBonusPeriod) + " days**");
			}

		});
	});

	RegisterCommand(Commands::CURRENCY_COMPENSATE, "compensate", [this](Discord::Client& client, const Discord::Message& message, const Discord::Channel& channel) 
	{

		QStringList args = message.content().split(' ');

		Permissions::ContainsPermission(client, channel.guildId(), message.author().id(), CommandPermission::ADMIN,
		[this, args, &client, message, channel](bool result) 
		{
			if (!result) 
			{
				client.createMessage(message.channelId(), "**You don't have permissions to use this command.**");
				return;
			}

			if (args.size() != 2) 
			{
				client.createMessage(message.channelId(), "**Wrong Usage of Command!** ");
				return;
			}

			auto& config = getServerData(channel.guildId());
			auto amt = args.at(1).toDouble();

			for (auto& user : guildList[channel.guildId()]) 
			{
				user.setCurrency(user.currency() + amt);
			}

			client.createMessage(message.channelId(), "**Everyone has been compensated with `" + QString::number(amt) + config.currencySymbol + "`**\nSorry for any inconvenience!");
		});

	});
}

void CurrencyModule::StatusCommand(QString& result, snowflake_t guild, snowflake_t user) 
{
	QString creditScore = QString::number(getUserData(guild, user).currency());
	auto& config = getServerData(guild);

	result += "Wallet: " + creditScore + " " + getServerData(guild).currencySymbol;
	result+='\n';
}

void CurrencyModule::OnMessage(Discord::Client& client, const Discord::Message& message) 
{

	client.getChannel(message.channelId()).then(
		[this, message, &client](const Discord::Channel& channel) 
		{
			if (channel.guildId() != 0 && !message.author().bot()) 
				//! Make sure the message is not a DM
			{
				auto guildId = channel.guildId();
				auto& serverConfig = getServerData(guildId);

				if (!serverConfig.isRandomGiveawayDone && !serverConfig.allowGiveaway) 
				{
						randp.param(std::bernoulli_distribution::param_type(serverConfig.randGiveawayProb));
						bool outcome = randp(random_engine);
						if (outcome) {
							client.createMessage(serverConfig.giveawayChannelId, "Hey everyone! **FREEBIE** available now! Go `!claim` some juicy coins!");
							serverConfig.allowGiveaway = true;
						}
				
				}


				//! If the message is a number, continue with the gambling mech
				QRegExp re("\\d*");
				if (re.exactMatch(message.content())) 
				{
		
					auto gambleAllowed = gambleData[guildId].gamble;
					auto isbotAuthor = message.author().bot();

					auto isGambleChannel = message.channelId() == gambleData[guildId].channelId;

					auto isUserGambler = message.author().id() == gambleData[guildId].userId;

					if (gambleAllowed && !isbotAuthor && isGambleChannel && isUserGambler)
					{
						auto guess = message.content().toInt();

						if (guess > serverConfig.maxGuess || guess < serverConfig.minGuess)
						{

							client.createMessage(message.channelId(), "**Your guess is out of range!** \nTry a number between " + QString::number(serverConfig.minGuess) + " and " + QString::number(serverConfig.maxGuess) + " (inclusive). ");
							return;

						}
						auto playerWon = guess == gambleData[guildId].randNum;
						if (playerWon) 
						{

							auto prize = gambleData[guildId].doubleOrNothing ? 2 * gambleData[guildId].betAmount : serverConfig.gambleReward;

							auto index = getUserIndex(guildId, 
								message.author().id());
							guildList[guildId][index].setCurrency(guildList[guildId][index].currency() + prize);
							client.createMessage(message.channelId(),
								"**You guessed CORRECTLY!**\n(**" +
								QString::number(prize) +
								serverConfig.currencySymbol + 
								"** have been added to your wallet!)");

						}
						else 
						{

							auto loss = gambleData[guildId].doubleOrNothing ? gambleData[guildId].betAmount : serverConfig.gambleLoss;
							auto symbol = serverConfig.currencySymbol;

							auto index = getUserIndex(guildId, message.author().id());
							guildList[guildId][index].setCurrency(guildList[guildId][index].currency() - loss);
							client.createMessage(message.channelId(), "**Better Luck next time! The number was `" + QString::number(gambleData[guildId].randNum) +"`**\n*(psst! I took **" + QString::number(loss) + symbol + "** from your wallet for my time...)*");

						}

						gambleData[guildId].gamble = false;
						gambleData[guildId].doubleOrNothing = false;
						delete gambleData[guildId].timer;
					}
				}
			}

		});

	Module::OnMessage(client, message);
}

void CurrencyModule::setSafeMode(Discord::Channel channel, Discord::Message msg, bool on)
{
	auto guildID = channel.guildId();
	auto authorId = msg.author().id();

	if (on)
	{
		guildList[guildID][getUserIndex(guildID, authorId)].canSteal = false;
		guildList[guildID][getUserIndex(guildID, authorId)].canClaimFreebies = false;
	}
	else 
	{
		guildList[guildID][getUserIndex(guildID, authorId)].canSteal = true;
		guildList[guildID][getUserIndex(guildID, authorId)].canClaimFreebies = true;
	}
}

void CurrencyModule::setSafeMode(Discord::Channel channel, Discord::Message msg, bool on)
{
	auto guildID = channel.guildId();
	auto authorId = msg.author().id();

	if (on)
	{
		guildList[guildID][getUserIndex(guildID, authorId)].canSteal = false;
		guildList[guildID][getUserIndex(guildID, authorId)].canClaimFreebies = false;
	}
	else 
	{
		guildList[guildID][getUserIndex(guildID, authorId)].canSteal = true;
		guildList[guildID][getUserIndex(guildID, authorId)].canClaimFreebies = true;
	}
}

void CurrencyModule::OnSave(QJsonDocument& doc) const 
{
	QJsonObject docObj;

	//! User Data
	for (auto server : guildList.keys()) 
	{
		QJsonObject serverJSON;
		
		for (auto user = guildList[server].begin(); user != guildList[server].end(); user++)
		{
			QJsonObject obj;
			obj["currency"] = user->currency();
			obj["maxCurrency"] = user->maxCurrency;
			obj["isDailyClaimed"] = user->isDailyClaimed;
			obj["dailyStreak"] = (int) user->dailyStreak;
			obj["numberOfDailysClaimed"] = (int) user->numberOfDailysClaimed;
			obj["numberOfGiveawaysClaimed"] = (int) user->numberOfGiveawaysClaimed;

			serverJSON[QString::number(user->userId)] = obj;
		}

		docObj[QString::number(server)] = serverJSON;
	}

	doc.setObject(docObj);
	

	//! Server Data (Config)
	QFile currenConfigfile("configs/" + currenConfigLoc + ".json");
	if (currenConfigfile.open(QFile::ReadWrite | QFile::Truncate)) 
	{
		QJsonDocument doc;
		QJsonObject serverList;
		for (auto server : serverCurrencyConfig.keys())
		{
			QJsonObject obj;
			
			auto config = serverCurrencyConfig[server];
			obj["name"] = config.currencyName;
			obj["symbol"] = config.currencySymbol;
			obj["freebieChannelId"] = QString::number(config.giveawayChannelId);
			obj["dailyReward"] = QString::number(config.dailyReward);
			obj["freebieReward"] = QString::number(config.freebieReward);
			obj["gambleLoss"] = QString::number(config.gambleLoss);
			obj["gambleReward"] = QString::number(config.gambleReward);
			obj["gambleMinGuess"] = QString::number(config.minGuess);
			obj["gambleMaxGuess"] = QString::number(config.maxGuess);
			obj["freebieProb"] = QString::number(config.randGiveawayProb);
			obj["freebieExpireTime"] = QString::number(config.freebieExpireTime);
			obj["dailyBonusAmount"] = QString::number(config.dailyBonusAmount);
			obj["dailyBonusPeriod"] = QString::number(config.dailyBonusPeriod);
			obj["stealSuccessChance"] = QString::number(config.stealSuccessChance);
			obj["stealFinePercent"] = QString::number(config.stealFinePercent);
			obj["stealVictimBonusPercent"] = QString::number(config.stealVictimBonusPercent);
			obj["stealFailedJailTime"] = QString::number(config.stealFailedJailTime);
			obj["bribeMaxAmount"] = QString::number(config.bribeMaxAmount);
			obj["bribeLeastAmount"] = QString::number(config.bribeLeastAmount);
			obj["bribeSuccessChance"] = QString::number(config.bribeSuccessChance);

			serverList[QString::number(server)] = obj;
			
		}
		doc.setObject(serverList);
		currenConfigfile.write(doc.toJson());
		currenConfigfile.close();
	}
}

void CurrencyModule::OnLoad(const QJsonDocument& doc) 
{
	QJsonObject docObj = doc.object();

	QStringList servers = docObj.keys();

	guildList.clear();

	//!User Data (Currency)
	for (auto server : servers) 
	{
		auto guildId = server.toULongLong();
		auto obj = docObj[server].toObject();
		QStringList users = obj.keys();

		QList<UserCurrency> list;
		for (auto user : users) {
			UserCurrency currencyData {
				user.toULongLong(),
				obj[user].toObject()["currency"].toDouble(),
				obj[user].toObject()["maxCurrency"].toDouble(),
				obj[user].toObject()["isDailyClaimed"].toBool(),
				(unsigned int) obj[user].toObject()["dailyStreak"].toInt(),
				(unsigned int) obj[user].toObject()["numberOfDailysClaimed"].toInt(),
				(unsigned int) obj[user].toObject()["numberOfGiveawaysClaimed"].toInt(),
			};

			list.append(currencyData);
		}
		guildList.insert(guildId, list);
		
	}
	QFile currenConfigfile("configs/" + currenConfigLoc + ".json");
	if (currenConfigfile.open(QFile::ReadOnly)) 
	{
		QJsonDocument d = QJsonDocument::fromJson(currenConfigfile.readAll());
		QJsonObject rootObj = d.object();

		serverCurrencyConfig.clear();
		auto servers = rootObj.keys();
		for (const auto& server : servers) {
			CurrencyConfig config;
			auto serverObj = rootObj[server].toObject();
			config.currencyName = serverObj["name"].toString();
			config.currencySymbol = serverObj["symbol"].toString();
			config.giveawayChannelId = serverObj["freebieChannelId"].toString().toULongLong();
			config.dailyReward = serverObj["dailyReward"].toString().toInt();
			config.freebieReward = serverObj["freebieReward"].toString().toInt();
			config.gambleLoss = serverObj["gambleLoss"].toString().toInt();
			config.gambleReward = serverObj["gambleReward"].toString().toInt();
			config.minGuess = serverObj["gambleMinGuess"].toString().toInt();
			config.maxGuess = serverObj["gambleMaxGuess"].toString().toInt();
			config.randGiveawayProb = serverObj["freebieProb"].toString().toDouble();
			config.freebieExpireTime = serverObj["freebieExpireTime"].toString().toUInt();
			config.dailyBonusAmount = serverObj["dailyBonusAmount"].toString("50").toUInt();
			config.dailyBonusPeriod = serverObj["dailyBonusPeriod"].toString("3").toUInt();
			config.stealSuccessChance = serverObj["stealSuccessChance"].toString("30").toUInt();
			config.stealFinePercent = serverObj["stealFinePercent"].toString("50").toUInt();
			config.stealVictimBonusPercent = serverObj["stealVictimBonusPercent"].toString("25").toUInt();
			config.stealFailedJailTime = serverObj["stealFailedJailTime"].toString("3").toUInt();
			config.bribeSuccessChance = serverObj["bribeSuccessChance"].toString("68").toUInt();
			config.bribeMaxAmount = serverObj["bribeMaxAmount"].toString("150").toInt();
			config.bribeLeastAmount = serverObj["bribeLeastAmount"].toString("20").toInt();
			config.bribeSuccessChance = serverObj["bribeSuccessChance"].toString("68").toUInt();
			config.bribeMaxAmount = serverObj["bribeMaxAmount"].toString("150").toInt();
			config.bribeLeastAmount = serverObj["bribeLeastAmount"].toString("20").toInt();

			auto guildId = server.toULongLong();
			serverCurrencyConfig.insert(guildId, config);
		}

		
		currenConfigfile.close();
	}
}
