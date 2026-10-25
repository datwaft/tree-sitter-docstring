"""Module documentation.

Attributes:
    version (str): Build version.
"""

ordinary = """Args:
    fake: This is an ordinary string.
"""

class Store:
    # Comments do not prevent the first statement from being a docstring.
    """Store documentation.

    Attributes:
        cache (dict[str, int]): Cached records.
    """

    async def fetch(self, key):
        """Fetch records.

        Args:
            key (str): Record key.
                A continuation.
        Returns:
            list[Record]: Matching records.

        Raises:
            ValueError: The key is invalid.
        """
        other = """Args:
            another_fake: This is also an ordinary string.
        """
        return []

if True:
    """Args:
        block_fake: A string in an arbitrary block is not a docstring.
    """
