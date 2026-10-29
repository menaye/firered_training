# firered_training

## Motivation

Ever since I was little, I found the post-game battle facilities in Pokémon games to be insurmountable. Strategies that I used to cruise through the main game simply were not working,
and I received very little feedback from the game as to why. Through growing up and coming to understand intentional design through my academic studies, I wanted to design a Pokémon game in such a way that allows players to be ready for these battle facilities and their strategies upon completion of the game. Introducing.... FireRed Training!

I consider the design of this ROMhack to be adjacent to games like *Celeste*, which introduces players to levels where they have to learn a new way to traverse the map. As the level progresses, the game tests the player's understanding bit by bit, while presenting optional challenges on that concept after the level is cleared. Similarly, there is a depth to the mechanics of Pokémon battling that can be explored through the natural progression of the game in its 8 gym leaders and league battles. By building teams strong enough to overcome the challenges and restrictions in the main game, the player should develop a strong enough grasp on battle strategies to challenge postgame facilities with no issues.

## Features

### A Complete FR/LG Experience, All in One Game

The primary focus of this ROMhack is to keep the spirit of FireRed and LeafGreen, while providing a fresh experience for those looking for greater difficulty in the main game. To this end, efforts have been made to retain the existing story and game balance in terms of levels and expected movesets, while raising the caliber of the opponents through smarter AI and team builds. Play with healing items mid-battle for a casual experience in line with the original games, or work with the challenge to test and improve your skills as a Trainer.

Build the team you want, however you want, regardless of your choices. 

- Encounter tables have been adjusted so there are no version exclusive Pokémon.
- No need to worry about trade exclusives, either; all have been adjusted to evolve by level.
- Starter choice and starter-dependent legendary encounters have been tweaked to allow for you to obtain them all.

### Level Cap

Many challenges in the game can be overcome by increasing your Pokémon's level and stats above the opponent's level. To ensure a level playing field, your team's levels will be capped to a specific level to be at the level of the next boss trainer's strongest Pokémon, and will be raised once they are defeated.

### Revamped Boss Trainer Teams

Boss trainers have had their teams, items, and movesets revamped to better reflect the design philosophy, with many of them employing competitive-viable movesets and strategies to be a fair, yet effective challenge. You can also rematch them after clearing the post-game, with formidable teams to be expected of a Gym Leader!

### Adjusted QOL

- Running Shoes will be available at the start, as well as useful items like the EXP Share.
- Overworld poison damage leaves your Pokémon at 1 HP instead of making them faint.
- TMs are now unlimited use to encourage experimentation.
- Items found in the overworld are stronger to incentivize exploring the map.

### Optional Boss Encounters

After beating a gym leader, an NPC can be located in their city that tests your understanding of competitive battling concepts. Use the preset team for the encounter, figure out a winning strategy, and employ it to prove your battle ability.

## Usage

Prerequisites: a GBA emulator of your choice.

> Note that there still exist differences between FireRed and LeafGreen, such as visual differences and Deoxys (Attack/FR, Defense/LG) forms. Both ROMs are able to be created with this file, so choose according to whichever sounds most appealing to you.

1. clone this repository with `git clone`
2. Navigate to `firered_romhack`
3. Enter `make firered` or `make leafgreen` in your terminal. This should take a few minutes; upon completion, a `.gba` file should be present in your current directory.
4. Open your GBA emulator and select the `.gba` ROM. Enjoy playing!

## Credits

The source code for Pokemon FireRed was decompiled by _______ and used as starter code for this project.
