return {
    "nvim-neo-tree/neo-tree.nvim",
    keys = {
        { "<C-n>", "<cmd>Neotree filesystem reveal left<cr>", desc = "Explorer (NeoTree)" },
    },
    opts = {
        filesystem = {
            filtered_items = {
                visible = true,
                hide_dotfiles = false,
                hide_gitignored = true,
            },
        },
    },
}
