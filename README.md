# Gunfire 3D Navigation

Gunfire3DNavigation is a plugin for generating voxelized navigation data for pathfinding in Unreal Engine 5. This is different from the stock Recast navigation mesh in that it can be used for 3D navigation, for flying or swimming creatures.

**This documentation is very basic and preliminary...**

After adding the plugin to your project, go to your project settings and in Navigation System, Supported Agents, add a new agent using the Gunfire3DNavData class. Ensure that agent is selected on your nav volume and regenerate navigation. Now you should be able to have a character using flying movement and the nav agent you added pathing in 3D space.

*Points: 3D Pathing Grid* can be used with the Environment Query System to find random reachable points.