{
    "components": {
        "Material": {
            "name": "TrollMaterial"
        },
        "Mesh": {
            "animation": "idle",
            "animation_loop": true,
            "animation_speed": 1.0,
            "clips": {
                "attack": "troll_attack",
                "death": "troll_death",
                "idle": "troll_idle",
                "walk": "troll_walk"
            },
            "name": "troll"
        },
        "Physics": {
            "shape": "CAPSULE",
            "type": "DYNAMIC"
        },
        "Transform": {
            "position": {
                "x": 0.0,
                "y": 0.0,
                "z": 0.0
            },
            "rotation": {
                "w": 0.7071067094802856,
                "x": -0.7071067690849304,
                "y": 0.0,
                "z": 0.0
            },
            "scale": {
                "x": 0.02500000037252903,
                "y": 0.02500000037252903,
                "z": 0.02500000037252903
            }
        }
    },
    "name": "troll",
    "parts": [
        {
            "attach": true,
            "bone": "mixamorig:LeftHand",
            "name": "test",
            "position": {
                "x": 0.0,
                "y": 0.0,
                "z": 0.0
            },
            "rotation": {
                "w": 1.0,
                "x": 0.0,
                "y": 0.0,
                "z": 0.0
            },
            "scale": {
                "x": 1.0,
                "y": 1.0,
                "z": 1.0
            },
            "template": "test"
        }
    ]
}