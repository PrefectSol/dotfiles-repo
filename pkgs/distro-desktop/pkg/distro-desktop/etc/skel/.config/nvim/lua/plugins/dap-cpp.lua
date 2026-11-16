return {
    "mfussenegger/nvim-dap",
    opts = function()
        for _, lang in ipairs({ "c", "cpp" }) do
            for _, c in ipairs(require("dap").configurations[lang] or {}) do
                if c.request == "launch" then
                    c.program = function()
                        return vim.fn.input("Path to executable: ", vim.fn.getcwd() .. "/build/", "file")
                    end
                end
            end
        end
    end,
}
